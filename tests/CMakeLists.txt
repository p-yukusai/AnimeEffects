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

# Round-trip project loader/saver harness (open -> serialize -> reload -> close).
# Takes .anie paths on the command line; used to exercise real project files.
add_executable(roundtrip roundtrip_main.cpp)
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

# V3 — export preset compatibility matrix. Parses assets/encode/VideoEncode.txt
# and asserts every preset maps to an available libav encoder and a supported
# pixel format (the compatibility gate). Headless. See media_preset_matrix.cpp.
add_executable(media_preset_matrix media_preset_matrix.cpp)
target_include_directories(media_preset_matrix PRIVATE ..)
target_compile_definitions(media_preset_matrix PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_VIDEOENCODE_PATH="${CMAKE_SOURCE_DIR}/assets/encode/VideoEncode.txt"
)
target_link_libraries(media_preset_matrix PRIVATE
    Qt6::Core
    Qt6::Xml
    media
)

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
