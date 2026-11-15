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
