# The tests are first-party code, so they use the project warning policy
# (see ae_enable_warnings in the top-level CMakeLists.txt): a test whose
# sections can silently not run is a test that reads as coverage.
ae_enable_warnings()

# Regression test for the BoneKey::deserialize use-after-free (dangling
# &mBindingCaches.back() captured into a deferred IDSolver lambda that runs
# after the QList reallocates). Self-contained; see bonekey_regression.cpp.
add_executable(bonekey_regression bonekey_regression.cpp)
target_include_directories(bonekey_regression PRIVATE ..)
target_compile_definitions(bonekey_regression PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(bonekey_regression PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Regression test for the util::TreeUtil::deleteAll use-after-free write (the
# pre-fix body cleared the child list only AFTER deleting the children, so
# TreeChildren::clear() wrote 24 bytes into every already-freed child). Catches
# it with no sanitizer via a poison-and-leak Node allocator. See
# treeutil_delete_uaf.cpp.
#
# It exercises only header-only code (util/TreeUtil.h + util/TreeNodeBase.h), so
# it needs no library beyond Qt6::Core -- no `util` (which would drag in Widgets/
# Gui/OpenGL) and no other module. The `../src` include dir is what resolves
# "XCAssert.h" and "util/...", the same way the `util` target exports it.
add_executable(treeutil_delete_uaf treeutil_delete_uaf.cpp)
target_include_directories(treeutil_delete_uaf PRIVATE ../src)
target_link_libraries(treeutil_delete_uaf PRIVATE Qt6::Core)

# Regression test for the PackBits noop packet (util::PackBits::decode,
# src/util/PackBits.cpp): a 0x80 header is one byte of "copy nothing" and has to
# consume that byte, but the loop re-read it forever -- so a single 0x80 anywhere
# in a project's image data hung the load on one core with no progress instead of
# reporting a decode error. PackBits.cpp is compiled in directly, so, like
# treeutil above, this needs no library beyond Qt6::Core. See packbits_decode.cpp.
add_executable(packbits_decode packbits_decode.cpp ../src/util/PackBits.cpp)
target_include_directories(packbits_decode PRIVATE ../src)
target_link_libraries(packbits_decode PRIVATE Qt6::Core)

# Regression test for the line length check in core::Deserializer::readImage: the
# length comes from the file and nothing compared it with the per-line buffer
# sized from the image width, so a damaged project made the loader copy an
# arbitrary length into that block. Drives the real Deserializer over a crafted
# stream and asserts the refusal (and, as a control, that a well-formed stream is
# accepted). See readimage_line_length.cpp.
add_executable(readimage_line_length readimage_line_length.cpp)
target_include_directories(readimage_line_length PRIVATE ..)
target_compile_definitions(readimage_line_length PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(readimage_line_length PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Regression test for the noop packet in the PSD importer's own PackBits reader
# (img::PSDUtil::decodePlanePackBits): a 0x80 header was re-read forever, so a
# damaged .psd hung the import instead of failing it. See psd_rle_noop.cpp.
add_executable(psd_rle_noop psd_rle_noop.cpp)
target_include_directories(psd_rle_noop PRIVATE ..)
target_compile_definitions(psd_rle_noop PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(psd_rle_noop PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    gl img thr util pugixml
)

# Contract test for the picker's fuzzy search (src/gui/qml/FuzzyMatch.h): each rule
# that must fire, the two that must not (rule 6's length gate and span guard), the
# ordering between rules, and the rule that is deliberately absent -- no rule may
# match a subsequence spanning several words, because " capped size limit" sits on
# every capped haystack and a cross-word subsequence would stop the search narrowing.
# The header is header-only, so, like treeutil above, this needs no library beyond
# Qt6::Core. `..` is the repository root (the header lives under src/gui/qml).
add_executable(fuzzy_match fuzzy_match.cpp)
target_include_directories(fuzzy_match PRIVATE ..)
target_link_libraries(fuzzy_match PRIVATE Qt6::Core)

# Contract test for the cmnd::Gesture seam (src/cmnd/Gesture.h): the open edit that
# owns one coalescing command. Pins the semantics every editor relies on -- the
# builder runs inside the edit's macro, the gate (continues()) survives in-place
# rewrites, undo/redo/clear close the edit, the grabbed listener executes once per
# commit, and a gesture that is never committed pushes nothing. Headless: QtCore
# only, no display, no assets. See cmnd_gesture.cpp.
add_executable(cmnd_gesture cmnd_gesture.cpp)
target_include_directories(cmnd_gesture PRIVATE ..)
target_link_libraries(cmnd_gesture PRIVATE cmnd)

# Round-trip project loader/saver harness (open -> serialize -> reload -> close).
# Takes .anie paths on the command line; used to exercise real project files.
#
# It needs the embedded resources: loading a project initialises GL, which reads
# :/shaders/*.glsl. Without assets.qrc the harness dies with
# "XC_FATAL FileIO Error No such file or directory :/shaders/MeshTransformVert.glsl"
# on the first real project — i.e. the one tool whose job is to round-trip real
# projects could not open one. The sibling GL harness (media_exporter_e2e) has
# always embedded them; this one was left behind when the assets moved from a
# data directory on disk into the binary.
add_executable(roundtrip roundtrip_main.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc
)
target_include_directories(roundtrip PRIVATE ..)
target_compile_definitions(roundtrip PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(roundtrip PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Right-click key selection: an unselected key becomes the selection for
# the context menu; also pins the surface->model coordinate mapping the
# widget must apply to context-menu positions. See
# rightclick_transient_selection.cpp. Compiles the real theme::TimeLine so
# ctrl's renderer links without pulling in the whole gui library.
add_executable(rightclick_transient_selection rightclick_transient_selection.cpp
    ../src/gui/theme/TimeLine.cpp)
target_include_directories(rightclick_transient_selection PRIVATE ..)
target_compile_definitions(rightclick_transient_selection PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(rightclick_transient_selection PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Contract test for the "creation selects its output" rule: pushing/copying a
# key (via the project timeline-modified signal) replaces the current selection
# with the created keys, and never does so on an undo or a non-creation event.
# See creation_select_keys.cpp.
add_executable(creation_select_keys creation_select_keys.cpp
    ../src/gui/theme/TimeLine.cpp)
target_include_directories(creation_select_keys PRIVATE ..)
target_compile_definitions(creation_select_keys PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(creation_select_keys PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Headless reproduction of the "Pull bones" (PoseEditMode_Draw -> pose::
# DrawBoneMode) crash. Loads a real .anie, builds the real ctrl::Driver with the
# Pose tool, and feeds synthetic left-button press/drag/release frames through
# core::AbstractCursor. Run under gdb (and xvfb-run for the GL context):
#   xvfb-run -a gdb -batch -ex run -ex "bt 25" --args \
#       build/tests/Debug/pose_pull_crash <file.anie>
# See pose_pull_crash.cpp.
add_executable(pose_pull_crash pose_pull_crash.cpp
    # ctrl::Driver owns ctrl::GraphEditor, whose renderQt() calls theme::Graph, and
    # the ctrl library leaves that symbol unresolved (the same reason graph_drag
    # and graph_render_pixels compile it). Without this the target links the graph
    # editor's object and fails on theme::Graph::from / keyColor.
    ../src/gui/theme/Graph.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(pose_pull_crash PRIVATE ..)
target_compile_definitions(pose_pull_crash PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(pose_pull_crash PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Regression test for the custom-bezier easing solver swap (WebKit UnitBezier
# port) and its control-point solver cache. See easing_regression.cpp.
find_package(Threads REQUIRED)
add_executable(easing_regression easing_regression.cpp)
target_include_directories(easing_regression PRIVATE ..)
target_compile_definitions(easing_regression PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(easing_regression PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    util
    Threads::Threads
)

# Code-based layout verification for the locked EasingPickWidget. Compiles the
# widget source directly (not the whole gui lib) so it links only util + Qt
# Widgets; the layout is asserted against tests/golden/easing_picker_geometry.json
# (the geometry measured from the browser reference).
add_executable(easing_picker_layout
    easing_picker_layout.cpp
    ../src/gui/prop/EasingPickWidget.cpp
    ../src/gui/prop/bezierCurveEditor.cpp
    ../src/gui/AppStyle.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/prop/EasingPickWidget.h)
target_include_directories(easing_picker_layout PRIVATE .. ../src/gui)
target_compile_definitions(easing_picker_layout PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(easing_picker_layout PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    util
)

# Interactive behavior probe for the EasingPickWidget (mode switch, slider
# drag-outside, knob clip, bezier spin input). Same source set as the layout
# gate; prints PASS/FAIL lines for each behavior.
# What the destination picker shows, offscreen: the list's own state, which is the
# part a person is affected by (what is selected when it opens, what typing selects,
# whether a platform can become an answer). It drives gui::DestinationPickerModel
# directly - no QWidget, no QTreeWidget, no QML engine - because the model is where
# the behaviour now lives and the Qt Quick scene binds to the same object. No
# display: the GUI probes ask the same question and need a bus and a window manager
# to do it, which is why this went unnoticed.
add_executable(destination_picker
    destination_picker.cpp
    ../src/gui/qml/DestinationPickerModel.cpp
    ../src/ctrl/ExportTarget.cpp)
target_include_directories(destination_picker PRIVATE .. ../src/gui)
target_compile_definitions(destination_picker PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_EXPORT_TARGETS_PATH="${CMAKE_SOURCE_DIR}/assets/export/ExportTargets.txt"
    # The .qm are build artifacts (not sources) and they are what the app actually
    # loads, so the language assertions need the built catalog and a dependency on
    # it: without release_translations this test would read a stale file and pass.
    AE_I18N_QM_DIR="${PROJECT_BINARY_DIR}/src/gui"
)
add_dependencies(destination_picker release_translations)
target_link_libraries(destination_picker PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml Qt6::Svg
    media
)

# The picker's KEYBOARD contract, offscreen and windowless: which key belongs to the
# search field's caret and which one moves the list. Neither of the two tests above
# can ask it - the model test has no scene, so nothing routes a key, and the GUI
# probes cannot inject a keystroke without a window manager. Measured before relying
# on it: a key sent straight to a Qt Quick item reaches the item's Keys filter with NO
# window and NO active focus, and everything the filter does not consume still reaches
# the editor. So this loads the real DestinationPicker.qml, injects the keys, and
# reads the model - the same object the scene binds to.
add_executable(picker_keyboard
    picker_keyboard.cpp
    ../src/gui/qml/DestinationPickerModel.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp
    ../src/ctrl/ExportTarget.cpp)
target_include_directories(picker_keyboard PRIVATE .. ../src ../src/gui)
target_compile_definitions(picker_keyboard PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_EXPORT_TARGETS_PATH="${CMAKE_SOURCE_DIR}/assets/export/ExportTargets.txt"
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
add_dependencies(picker_keyboard release_translations)
target_link_libraries(picker_keyboard PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    media
)

# The new-project dialog, headless: the model's defaults and clamps, and the scene's
# own wiring. The form used to be a QWidget/QFormLayout, so only a shown widget could
# be driven; it is a Qt Quick scene now, and both halves of the contract are asked
# without a window - the model directly, and the REAL NewProjectDialog.qml loaded and
# clicked so the "Live link" checkbox is proven to write back (AeCheckBox records that
# setting `checked` does not fire toggled(), so a click is the only honest driver).
add_executable(new_project_form new_project_form.cpp
    ../src/gui/qml/NewProjectViewModel.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp)
target_include_directories(new_project_form PRIVATE .. ../src ../src/gui)
target_compile_definitions(new_project_form PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
target_link_libraries(new_project_form PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    media
)

# The project-settings dialog, headless: the model's defaults and clamps (canvas
# dimensions bounded by the device buffer, fps floor, the frame range's
# start <= end invariant) and the scene's own wiring. Same shape as
# new_project_form: the model directly, and the REAL ProjectSettingDialog.qml
# loaded so the fields are proven to write back.
add_executable(project_setting_form project_setting_form.cpp
    ../src/gui/qml/ProjectSettingViewModel.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp)
target_include_directories(project_setting_form PRIVATE .. ../src ../src/gui)
target_compile_definitions(project_setting_form PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
target_link_libraries(project_setting_form PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    media
)

# The key-bindings dialog, headless: the model's capture rules (Delete/Backspace
# clears, a modifier-free key after an existing chord becomes the sub-key, a valid
# chord replaces, conflicts are reported both ways, reset-to-defaults restores),
# and that a real key press through KeyBindingDialog.qml writes the row.
# ctrl/KeyBinding.cpp is compiled in directly (the model is Core-only plus that
# type, the way destination_picker compiles ExportTarget.cpp).
add_executable(key_binding_form key_binding_form.cpp
    ../src/gui/qml/KeyBindingViewModel.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp
    ../src/ctrl/KeyBinding.cpp)
target_include_directories(key_binding_form PRIVATE .. ../src ../src/gui)
target_compile_definitions(key_binding_form PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
target_link_libraries(key_binding_form PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    media
)

# The general-settings dialog, headless: the model's defaults and that each edit
# flips the matching has-changed predicate the shell reports to the menu bar, plus
# the REAL GeneralSettingDialog.qml loaded so a control is proven to write back.
add_executable(general_setting_form general_setting_form.cpp
    ../src/gui/qml/GeneralSettingViewModel.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/MouseSetting.cpp)
target_include_directories(general_setting_form PRIVATE .. ../src ../src/gui)
target_compile_definitions(general_setting_form PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
target_link_libraries(general_setting_form PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    media
)

# The numeric fields' Escape contract (F34, decided 2026-09-30): Escape restores
# the value from when THIS edit began - not the value the property happens to hold
# at Escape time (the owner can re-sync it mid-edit) and not the session's opening
# value - with the AeNumberField sentinel ("Auto") as part of the same line.
# Loads the REAL ScrubSpinBox.qml and AeNumberField.qml and drives the edit
# lifecycle through a real offscreen window, because the lifecycle IS focus:
# beginEdit() focuses the editor, Escape blurs it, and onActiveFocusChanged is
# where the edit starts and ends. See scrub_escape.cpp for the measured routing
# facts this rests on.
add_executable(scrub_escape
    scrub_escape.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp)
target_include_directories(scrub_escape PRIVATE .. ../src ../src/gui)
target_compile_definitions(scrub_escape PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
target_link_libraries(scrub_escape PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    media
)

# C21's hover-fade rule (item 21): no wrong intermediate colour. The ghost
# button's rest fill must fade through its OWN RGB at varying alpha - the old
# "transparent" endpoint made the ColorAnimation pass through semi-transparent
# black ("Add range hover flashes black"). Samples the ANIMATED background.color
# property per frame (a grabToImage is blind here: it composites over opaque
# black and hides alpha - measured). See hover_parity.cpp.
add_executable(hover_parity
    hover_parity.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp)
target_include_directories(hover_parity PRIVATE .. ../src ../src/gui)
target_compile_definitions(hover_parity PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
target_link_libraries(hover_parity PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    media
)

# The tooltip contract (E26/E27/E28/E29, decided 2026-09-30): WHICH controls
# keep a tooltip (the set is explicit - the Auto/Reset affordances and truncated
# picker rows), WHERE the bubble sits (below-first, flipping above at the bottom
# edge) and that it is NEVER clipped at the dialog's edge. Hosts the REAL
# call-site files and the real ExportDialog.qml (so the set cannot silently
# grow) with the real view model. See tooltip_contract.cpp.
add_executable(tooltip_contract
    tooltip_contract.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/qml/ExportViewModel.cpp
    ../src/gui/qml/ExportViewModel.h
    ../src/gui/ExportFacts.cpp
    ../src/ctrl/ExportTarget.cpp
    # The embedded destination table (see export_form below: the dialog's own
    # lookups go through the static loaders to ":/export/ExportTargets.txt").
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(tooltip_contract PRIVATE .. ../src ../src/gui)
target_compile_definitions(tooltip_contract PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
add_dependencies(tooltip_contract release_translations)
target_link_libraries(tooltip_contract PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    media
)

# The export form's overflow contract (D25 + Q9): the content scrolls by wheel,
# drag and keys while the Cancel/Export row stays pinned and reachable, every
# control is scrolled into view on focus, the bar appears only on overflow, and
# the tooltip/combo popups inside the scrolling content are clipped by NOTHING
# (E28's clamp narrows to the visible viewport). Hosts the REAL ExportDialog.qml
# with the real view model. See form_layout.cpp.
add_executable(form_layout
    form_layout.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/qml/ExportViewModel.cpp
    ../src/gui/qml/ExportViewModel.h
    ../src/gui/ExportFacts.cpp
    ../src/ctrl/ExportTarget.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(form_layout PRIVATE .. ../src ../src/gui)
target_compile_definitions(form_layout PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
add_dependencies(form_layout release_translations)
target_link_libraries(form_layout PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test
    media
)

# Persistence: C17 (group expansion remembered across sessions) and A5 (the
# dialog remembers its size). Drives the MACHINERY (DestinationPickerModel's
# expandedIds/applyExpandedIds) and the WIRING (gui::DestinationPicker and
# gui::ExportDialog reading and writing QSettings) through the real dialog
# classes. Their QML scenes live in the application's qml module and are not
# linked here (the scene routes are picker_keyboard's and the probes' job).
add_executable(persist_state
    persist_state.cpp
    ../src/gui/DestinationPicker.cpp
    ../src/gui/ExportDialog.cpp
    ../src/gui/GUIResources.cpp
    ../src/gui/AppStyle.cpp
    ../src/gui/qml/QmlFormAccessibility.cpp
    ../src/gui/qml/DestinationPickerModel.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/qml/ExportViewModel.cpp
    ../src/gui/theme/Theme.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/ExportFacts.cpp
    ../src/ctrl/ExportTarget.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(persist_state PRIVATE .. ../src ../src/gui)
target_compile_definitions(persist_state PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_EXPORT_TARGETS_PATH="${CMAKE_SOURCE_DIR}/assets/export/ExportTargets.txt"
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
add_dependencies(persist_state release_translations)
target_link_libraries(persist_state PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickWidgets Qt6::QuickControls2
    media
)

# The export dialog, headless: what it STATES and what it OFFERS per destination.
# This is the coverage hole the session named twice: every export-form bug was either
# a fact contradicting a control or a control the destination did not decide, and both
# are questions about state and text rather than about pixels. Since the port to Qt
# Quick it drives gui::ExportViewModel directly - no QWidget, no offscreen show, no
# isVisible() - so its sources compile GUI-free: no GL, no Widgets, no Quick. See
# tests/export_form.cpp.
add_executable(export_form
    export_form.cpp
    ../src/gui/ExportFacts.cpp
    ../src/gui/qml/ExportViewModel.cpp
    ../src/gui/qml/ExportViewModel.h
    ../src/ctrl/ExportTarget.cpp
    # The destination TABLE, embedded the way the application embeds it.
    #
    # The table is loaded two ways: this test passes a path (AE_EXPORT_TARGETS_PATH) for
    # the plan and the facts, while the DIALOG's own lookups - the codec's quality span,
    # the quality buckets, the time budgets - call the static loaders, whose default path
    # is the embedded ":/export/ExportTargets.txt". Without this resource the test's
    # dialog sees an EMPTY table: every codec looks like it has no quality knob, the
    # override control is never offered (measured: 0 of 56 rows), and the assertions that
    # ask about it were skipped for every row while the suite stayed green. A dialog test
    # that reads a different table than the application is not testing the application.
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(export_form PRIVATE .. ../src ../src/gui)
target_compile_definitions(export_form PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/assets"
    AE_EXPORT_TARGETS_PATH="${CMAKE_SOURCE_DIR}/assets/export/ExportTargets.txt"
    AE_EXPORT_STRINGS_PATH="${CMAKE_SOURCE_DIR}/src/ctrl/ExportTargetStrings.h"
    # .qm are build artifacts (release_translations); the runtime translation
    # assertion loads one from the build output, never from source.
    AE_I18N_QM_DIR="${PROJECT_BINARY_DIR}/src/gui"
)
target_link_libraries(export_form PRIVATE
    Qt6::Core Qt6::Gui Qt6::Xml Qt6::Svg
    media
)
# The runtime translation assertion reads a BUILT catalog, so the test has to depend on
# the catalog being rebuilt: without this it can run against a stale .qm and report that
# the table arrives untranslated, which is exactly what happened the first time.
add_dependencies(export_form release_translations)

add_executable(picker_probe
    picker_probe.cpp
    ../src/gui/prop/EasingPickWidget.cpp
    ../src/gui/prop/bezierCurveEditor.cpp
    ../src/gui/AppStyle.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/prop/EasingPickWidget.h)
target_include_directories(picker_probe PRIVATE .. ../src/gui)
target_compile_definitions(picker_probe PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    AE_ICONS_DIR="${CMAKE_SOURCE_DIR}/assets/icons"
    USE_GL_CORE_PROFILE
)
target_link_libraries(picker_probe PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    util
)

# Screenshot harness for the EasingPickWidget: renders the popup with every
# text glyph hidden (setHideText, alpha-0 text so the geometry is unchanged)
# in the normal In-Out state and in the Custom (bezier) state, writing the two
# PNGs so the Qt replica can be compared with the browser mock at a glance.
# Same source set as the layout gate / behavior probe.
add_executable(easing_picker_shot
    easing_picker_shot.cpp
    ../src/gui/prop/EasingPickWidget.cpp
    ../src/gui/prop/bezierCurveEditor.cpp
    ../src/gui/AppStyle.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/prop/EasingPickWidget.h)
target_include_directories(easing_picker_shot PRIVATE .. ../src/gui)
target_compile_definitions(easing_picker_shot PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(easing_picker_shot PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    util
)

# Popup-position probe for the EasingPickWidget: shows the picker as a real
# top-level Qt::Popup, toggles Custom<->normal, and asserts the left edge does
# not drift rightward (the width is fixed; only the height should change).
add_executable(picker_pos_probe
    picker_pos_probe.cpp
    ../src/gui/prop/EasingPickWidget.cpp
    ../src/gui/prop/bezierCurveEditor.cpp
    ../src/gui/AppStyle.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/prop/EasingPickWidget.h)
target_include_directories(picker_pos_probe PRIVATE .. ../src/gui)
target_compile_definitions(picker_pos_probe PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(picker_pos_probe PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    util
)

# Observable project attribute: setting an attribute value drives
# onProjectAttributeModified (and an unchanged set does not), so a settings
# dialog can apply a value live and scope undo to a separate command grain. See
# attribute_observable.cpp.
add_executable(attribute_observable attribute_observable.cpp)
target_include_directories(attribute_observable PRIVATE ..)
target_compile_definitions(attribute_observable PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(attribute_observable PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Animation-settings ViewModel session undo/redo: discrete edits record one
# step, a scrub gesture coalesces into one, and reset() clears history per open.
# See anim_settings_undo.cpp.
add_executable(anim_settings_undo
    anim_settings_undo.cpp
    ../src/gui/qml/AnimationSettingViewModel.cpp
    ../src/gui/qml/AnimationSettingViewModel.h)
target_include_directories(anim_settings_undo PRIVATE .. ../src ../src/gui)
target_compile_definitions(anim_settings_undo PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(anim_settings_undo PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
)

# QML-module i18n: the Animation settings dialog's qsTr() strings must resolve
# through the SAME QTranslator the app installs on QCoreApplication, using the
# module's explicit pragma Translator context (AnimeEffects.Dialogs). Verifies
# the catalog content under that context AND that a QML qsTr() under the same
# pragma renders the translation at runtime. See anim_setting_i18n.cpp. Run
# from the repo root:
#   QT_QPA_PLATFORM=offscreen ./build/tests/Debug/anim_setting_i18n ja
add_executable(anim_setting_i18n anim_setting_i18n.cpp)
target_include_directories(anim_setting_i18n PRIVATE ..)
target_compile_definitions(anim_setting_i18n PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    # .qm are build artifacts (release_translations); load them from the build
    # output dir, never from the committed source data/.
    AE_I18N_QM_DIR="${PROJECT_BINARY_DIR}/src/gui"
)
target_link_libraries(anim_setting_i18n PRIVATE
    Qt6::Core Qt6::Gui
    Qt6::Qml Qt6::Quick
)
# Mirror the app: the .qm are build artifacts embedded under :/i18n, so the
# test exercises the same resource path the app loads at runtime (not just a
# raw file from the build dir).
qt_add_resources(anim_setting_i18n "translations"
    PREFIX "/i18n"
    BASE "${PROJECT_BINARY_DIR}/src/gui"
    FILES
        ${PROJECT_BINARY_DIR}/src/gui/translation_en.qm
        ${PROJECT_BINARY_DIR}/src/gui/translation_es.qm
        ${PROJECT_BINARY_DIR}/src/gui/translation_ja.qm
        ${PROJECT_BINARY_DIR}/src/gui/translation_ko.qm
        ${PROJECT_BINARY_DIR}/src/gui/translation_ru.qm
        ${PROJECT_BINARY_DIR}/src/gui/translation_zh.qm
)
add_dependencies(anim_setting_i18n release_translations)

# V0 — build/link gate for the media (libav*) rearchitecture. Links the `media`
# module directly (GL-free) and asserts the libav* libraries link + report a
# usable encoder set, and whether GPL encoders (libx264/libx265) are available.
# Headless; no QApplication. See media_link_probe.cpp.
add_executable(media_link_probe media_link_probe.cpp)
target_include_directories(media_link_probe PRIVATE ..)
target_compile_definitions(media_link_probe PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(media_link_probe PRIVATE
    Qt6::Core
    media
)

# V1 — media::VideoEncoder correctness + edge cases. Uses the `media` module
# (libav*) to encode and round-trips output back through the same linked
# decode path, asserting pixel-exact lossless fidelity + structural/metadata
# edge cases. Headless (offscreen). See media_video_encoder.cpp.
add_executable(media_video_encoder media_video_encoder.cpp)
target_include_directories(media_video_encoder PRIVATE ..)
target_compile_definitions(media_video_encoder PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(media_video_encoder PRIVATE
    Qt6::Core
    Qt6::Gui
    media
)

# V2 — media::AudioMixer multi-track correctness + edge cases. Uses the `media`
# module (libav*) to decode/mix real PCM WAV tracks, asserting the mixed output
# against the analytic sum (offsets/volumes/overlap) plus edge cases and rate
# conversion. Headless. See media_audio_mixer.cpp.
add_executable(media_audio_mixer media_audio_mixer.cpp)
target_include_directories(media_audio_mixer PRIVATE ..)
target_compile_definitions(media_audio_mixer PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(media_audio_mixer PRIVATE
    Qt6::Core
    media
)

# Export-destination table prototype: loads assets/export/ExportTargets.txt,
# validates every row against the linked libav* build, checks the size/cap
# rules, resolves the codec ladder, and encodes real files from resolved plans.
# Compiles ExportTarget.cpp directly (GL-free) so it links only media + Qt
# Core/Gui/Xml. Headless. See media_export_target.cpp.
add_executable(media_export_target
    media_export_target.cpp
    ../src/ctrl/ExportTarget.cpp
    ../src/ctrl/ExportTarget.h
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(media_export_target PRIVATE ..)
target_compile_definitions(media_export_target PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_EXPORT_TARGETS_PATH="${CMAKE_SOURCE_DIR}/assets/export/ExportTargets.txt"
    AE_EXPORT_STRINGS_PATH="${CMAKE_SOURCE_DIR}/src/ctrl/ExportTargetStrings.h"
)
target_link_libraries(media_export_target PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Xml
    media
)
# (Warning flags come from the directory-wide ae_enable_warnings() call at the
# top of this file; this target used to set -Wall -Wextra on its own, from when
# it was the only target that had them.)

# End-to-end export harness: offscreen (GL via a QOpenGLWidget) render of a
# minimal project -> ctrl::Exporter -> a real mp4, verified with ffprobe. This
# is the end-to-end gate for the media/export rearchitecture. Headless.
add_executable(media_exporter_e2e media_exporter_e2e.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc
)
target_include_directories(media_exporter_e2e PRIVATE ..)
target_compile_definitions(media_exporter_e2e PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(media_exporter_e2e PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    media cmnd core ctrl gl img thr util pugixml
)

# Regression test for the encoder open-time fallback (src/ctrl/Exporter.cpp): a
# hardware encoder that passes the small capability probe can still be refused at
# the real resolution/option set, and the export must step down to the same family's
# software encoder instead of aborting. The test makes nvenc fail device-independently
# by handing it an option only libx264 knows, so it stays red on a box that has working
# NVENC. See export_encoder_fallback.cpp.
add_executable(export_encoder_fallback export_encoder_fallback.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc
)
target_include_directories(export_encoder_fallback PRIVATE ..)
target_compile_definitions(export_encoder_fallback PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(export_encoder_fallback PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    media cmnd core ctrl gl img thr util pugixml
)

# Regression test for the PSD layer identity defects (src/img/PSDFormat.h,
# PSDReader.cpp, Util.cpp, ResourceData, ResourceNode, core/ImageKey.cpp):
# identity was the display name, the lazy loader captured a pointer into a
# stack-owned PSDFormat, and JSON resolution took the LAST matching tree. Drives
# the real reader and loader through a crafted PSD. See psd_layer_identity.cpp.
add_executable(psd_layer_identity psd_layer_identity.cpp)
target_include_directories(psd_layer_identity PRIVATE ..)
target_compile_definitions(psd_layer_identity PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(psd_layer_identity PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Contract test for the PSD live-link safety rules (src/gui/res/res_ResourceUpdater.cpp,
# src/gui/ResourceTreeWidget.cpp, src/core/ImageKeyUpdater.cpp, src/util/IDSolver.h):
# a relink had to be transactional (the failure paths returned out of a ScopedMacro
# whose destructor committed half a batch), forceReload compared the live tree with
# itself and then freed it, a layer missing from the source was silently kept or
# dropped, a name-colliding rename re-pointed one layer's content onto another's
# live node, and the file watcher ignored which file changed while accumulating a
# handler per watch cycle. Headless: QApplication, no GL. See psd_relink_safety.cpp.
add_executable(psd_relink_safety psd_relink_safety.cpp
    ../src/gui/ResourceTreeWidget.cpp
    ../src/gui/ProjectHook.cpp
    ../src/gui/ViaPoint.cpp
    ../src/gui/EasyDialog.cpp
    # The real ResourceDialog, not a stub: the test links its moc key functions,
    # and it needs no MainMenuBar at link time (only the type name in a connect).
    ../src/gui/ResourceDialog.cpp
    ../src/gui/res/res_ResourceUpdater.cpp
    ../src/gui/res/res_Item.cpp
    ../src/gui/res/res_Notifier.cpp)
target_include_directories(psd_relink_safety PRIVATE .. ../src ../src/gui)
target_compile_definitions(psd_relink_safety PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(psd_relink_safety PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Contract test for the animation graph's value model: ctrl::graph::Channel,
# ctrl::graph::Curve and ctrl::graph::Scaler (src/ctrl/graph). The sampler is
# compared against the real core::TimeKeyBlender over a real core::TimeLine and
# read out of the same core::TimeKeyExpans the renderer reads, so the graph can
# only pass by drawing what playback evaluates. Headless: no widgets, no GL, no
# .anie fixture, no extra source files (the one property that needs a GL
# context, the image-offset staircase, reports itself as a skip). See
# graph_curve.cpp.
add_executable(graph_curve graph_curve.cpp)
target_include_directories(graph_curve PRIVATE ..)
target_compile_definitions(graph_curve PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(graph_curve PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# ---------------------------------------------------------------------------
# The overlay's units (the Dear ImGui renderer of the viewport panel). They exist
# because the pieces a screenshot cannot see need gates of their own: the font
# stack, the Qt->ImGuiIO translation, the accessibility bridge and the panel
# model. The first two link ae_imgui (declared in the root CMakeLists, behind
# AE_ENABLE_IMGUI); the last two deliberately do not - the accessibility bridge is
# pure QAccessible over a plain model, and the panel model is geometry. All four
# are headless: offscreen, no GL context, no window.
#
# scripts/run_suites.sh discovers targets from this file, so registering one here
# is all that puts it on the gate.
# ---------------------------------------------------------------------------
if(AE_ENABLE_IMGUI)
    # The font loader: ImGui rasterizes through Qt (QRawFont), which is what makes
    # CJK, .ttc, CFF and variable fonts work. Builds an atlas and reads glyph
    # pixels - no GL. See src/gui/ImGuiQtFontLoader.h.
    add_executable(imgui_qt_font_loader imgui_qt_font_loader.cpp
        ../src/gui/ImGuiQtFontLoader.cpp)
    target_include_directories(imgui_qt_font_loader PRIVATE .. ../src)
    target_compile_definitions(imgui_qt_font_loader PRIVATE
        AE_MAJOR_VERSION=1
        AE_MINOR_VERSION=7
        AE_MICRO_VERSION=1
        USE_GL_CORE_PROFILE
    )
    target_link_libraries(imgui_qt_font_loader PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets
        ae_imgui
    )

    # The input bridge: mouse/keys/wheel, IME (commit and pre-edit), tablet, touch,
    # DPR, and the WantCapture consumption policy the application's key routing has
    # to obey. No GL, no window.
    add_executable(imgui_input_bridge imgui_input_bridge.cpp
        ../src/gui/ImGuiInputBridge.cpp)
    target_include_directories(imgui_input_bridge PRIVATE .. ../src)
    target_compile_definitions(imgui_input_bridge PRIVATE
        AE_MAJOR_VERSION=1
        AE_MINOR_VERSION=7
        AE_MICRO_VERSION=1
        USE_GL_CORE_PROFILE
    )
    target_link_libraries(imgui_input_bridge PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets
        ae_imgui
    )

    # The panel's PIXEL contract, and the replacement for graph_render_pixels (which
    # pins the painter renderer the cutover deletes). It drives gui::drawPanel through
    # a real ImGui frame into its own offscreen framebuffer and asserts the pixels the
    # model's geometry promises - including the INVERSE assertion that a plot area
    # holding no geometry is left untouched, which is what "no background, no grid"
    # means in pixels. Needs a real GL context, so it runs under xvfb (the GL branch
    # in scripts/run_suites.sh).
    #
    # There is no `gui` library to link against - the application target compiles
    # these files itself - so the overlay's own units come along as sources.
    # ImGuiSpikeGL.cpp must stay the first ImGui translation unit: its loader shim
    # precedes the backend.
    add_executable(graph_panel_pixels graph_panel_pixels.cpp
        ../src/gui/ImGuiGLBackend.cpp
        ../src/gui/ImGuiOverlay.cpp
        ../src/gui/GraphPanelModel.cpp
        ../src/gui/ImGuiInputBridge.cpp
        ../src/gui/ImGuiA11y.cpp
        ../src/gui/ImGuiQtFontLoader.cpp
        ../src/gui/theme/Graph.cpp)
    target_include_directories(graph_panel_pixels PRIVATE .. ../src)
    target_compile_definitions(graph_panel_pixels PRIVATE
        AE_MAJOR_VERSION=1
        AE_MINOR_VERSION=7
        AE_MICRO_VERSION=1
        USE_GL_CORE_PROFILE
        AE_ENABLE_IMGUI=1
    )
    target_link_libraries(graph_panel_pixels PRIVATE
        Qt6::Core Qt6::Core5Compat Qt6::Widgets
        Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
        Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
        cmnd core ctrl gl img thr util pugixml
        ae_imgui
    )
endif()

# The accessibility bridge for the overlay's framebuffer-drawn content. No ImGui
# dependency at all: it publishes a plain item model through QAccessible, and the
# live AT-SPI dump (scripts/gui/probe_graph_overlay.sh) is what finally checks it
# against a screen reader's view. See ImGuiA11y.h.
add_executable(imgui_a11y imgui_a11y.cpp
    ../src/gui/ImGuiA11y.cpp)
target_include_directories(imgui_a11y PRIVATE .. ../src)
target_compile_definitions(imgui_a11y PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(imgui_a11y PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
)

# The panel model: what the overlay draws, as engine-neutral geometry computed from
# ctrl::GraphEditor - and, crucially, asserted CONSISTENT with the editor's own
# panelRect/scale, because a model that computed its own rects would silently
# desync the pixels from the hit-test. Compiles theme::Graph for the same reason
# graph_drag does. See graph_panel_model.cpp.
add_executable(graph_panel_model graph_panel_model.cpp
    ../src/gui/GraphPanelModel.cpp
    ../src/gui/theme/Graph.cpp)
target_include_directories(graph_panel_model PRIVATE ..)
target_compile_definitions(graph_panel_model PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(graph_panel_model PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Contract test for the animation graph's EDIT path (ctrl::GraphEditor): one
# handle drag must be exactly ONE undo step, and it must edit the grabbed key's
# own util::Easing::Param. Drives the real editor with synthetic
# core::AbstractCursor frames and proves the command count by undo/redo rather
# than by inspection. Headless: offscreen, no GL, no .anie. Compiles the real
# theme::Graph because GraphEditor names it and the ctrl library leaves that
# symbol unresolved - the same reason the timeline tests compile
# theme/TimeLine.cpp. See graph_drag.cpp.
add_executable(graph_drag graph_drag.cpp
    ../src/gui/theme/Graph.cpp)
target_include_directories(graph_drag PRIVATE ..)
target_compile_definitions(graph_drag PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(graph_drag PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# The panel's pixels used to be pinned here, by driving the raster QPainter
# renderer (ctrl::graph::Renderer) into a QImage. That renderer is gone: the panel
# is drawn by gui::drawPanel (Dear ImGui), and its pixel contract lives in
# tests/graph_panel_pixels.cpp, which drives the real drawing through a GL frame and
# asserts the same primitives - plus the inverse assertion that an area with no
# geometry in it is untouched, which no test of the painter path could make.

# The image-offset channel's step-hold, in the one environment where it can be
# observed: constructing a core::ImageKey binds a GL buffer for its GridMesh, so
# tests/graph_curve.cpp (headless, offscreen) has to report that section as a
# skip. This test creates a context and asserts the staircase for real - the
# offset holds the last preceding key's value across the whole span, with no
# trace of the Custom easing the first key carries. Runs under xvfb.
add_executable(graph_image_step graph_image_step.cpp)
target_include_directories(graph_image_step PRIVATE ..)
target_compile_definitions(graph_image_step PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(graph_image_step PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# GUARD test for the paint engine that draws every viewport overlay
# (ctrl::PainterHandle -> GLCorePaintEngine in a USE_GL_CORE_PROFILE build). It
# drives the real ctrl::PainterHandle into its OWN offscreen framebuffer and
# counts pixels per probe, so the engine's ACTUAL primitive subset is pinned
# rather than assumed: the primitives the gizmos and the graph panel rely on must
# paint, and two silent traps must keep behaving as documented - a QPainterPath
# paints nothing (drawPath is `{ (void)aPath; }`), and a text draw with no brush
# scheduled paints nothing (pushDrawCommand drops usePen=false draws unless a
# brush is scheduled). Both cost real defects on 2026-10-12; see the measured
# notes in AGENTS.md. It reads its own FBO with glReadPixels, never
# QOpenGLWidget::grabFramebuffer() (which re-enters Qt's render and invalidates
# the buffer). Needs a real GL context, so it runs under xvfb (see the GL branch
# in scripts/run_suites.sh). assets.qrc is embedded for the engine's shaders.
add_executable(overlay_engine_primitives overlay_engine_primitives.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(overlay_engine_primitives PRIVATE ..)
target_compile_definitions(overlay_engine_primitives PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(overlay_engine_primitives PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Regression test for removing an asset that layers still use
# (core::ObjectTree::findNodesUsingResource, gui::ObjectTreeWidget::pushDeleteObjects,
# gui::res::ResourceUpdater::remove): the removal used to be refused outright when
# any layer rendered from the asset. It now deletes those layers in the same undo
# step, so the search must walk the asset's whole subtree, the resource must be
# released before its tree is removed, and ONE undo must restore both the asset and
# its layers IN ORDER. Builds the real object tree and asset tree and drives the
# real removal; needs a GL context for core::LayerNode's default ImageKey, so it
# runs under xvfb (cf. psd_layer_identity). See asset_remove_layers.cpp.
add_executable(asset_remove_layers asset_remove_layers.cpp
    ../src/gui/ObjectTreeWidget.cpp
    ../src/gui/GUIResources.cpp
    ../src/gui/AppStyle.cpp
    ../src/gui/ProjectHook.cpp
    ../src/gui/ViaPoint.cpp
    ../src/gui/EasyDialog.cpp
    ../src/gui/ResourceDialog.cpp
    ../src/gui/ResourceTreeWidget.cpp
    ../src/gui/res/res_ResourceUpdater.cpp
    ../src/gui/res/res_Item.cpp
    ../src/gui/res/res_Notifier.cpp
    ../src/gui/obj/obj_Item.cpp
    ../src/gui/obj/obj_InsertItem.cpp
    ../src/gui/obj/obj_MoveItem.cpp
    ../src/gui/obj/obj_Notifiers.cpp
    ../src/gui/obj/obj_RemoveItem.cpp
    ../src/gui/obj/obj_Util.cpp
    ../src/gui/theme/Theme.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/theme/TimeLine.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(asset_remove_layers PRIVATE .. ../src ../src/gui)
target_compile_definitions(asset_remove_layers PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(asset_remove_layers PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)
