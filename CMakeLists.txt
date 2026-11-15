cmake_minimum_required(VERSION 3.16)
project(AnimeEffects VERSION 1.7.1 LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# AddressSanitizer (with LeakSanitizer) opt-in: pretty bad perf hit.
# The known process-lifetime caches (fontconfig/Pango/GTK3 via the Qt
# gtk3 platform theme) are suppressed in-code via __lsan_default_suppressions()
# (see src/gui/LeakSuppressions.cpp), so no env vars or run scripts are
# needed — configure with -DAE_ENABLE_ASAN=ON and run the binary directly.
option(AE_ENABLE_ASAN "Build with AddressSanitizer and LeakSanitizer" OFF)
if(AE_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

include(GNUInstallDirs)
# You need to add the Qt directory to your path, otherwise you'll have a very bad time
# On MacOS Tahoe please remove AGL from the framework list when linking
# QuickControls2 is here for the migrated Qt Quick dialogs: their controls are
# QtQuick.Controls types with the design's own background/contentItem delegates.
# The Basic style is selected in Main.cpp (the default on Linux is Fusion, and
# Basic is the documented base for a customised look).
find_package(Qt6 REQUIRED COMPONENTS Core5Compat Core Gui OpenGL OpenGLWidgets Svg Xml Concurrent Widgets Multimedia Quick Qml QuickWidgets QuickControls2 LinguistTools)
# Qt's i18n CMake support: declares the translated languages lupdate/lrelease
# operate on, and enables the qt_add_translations/update_translations targets.
# The translation sources live in assets/locale/translation_<locale>.ts.
qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES es ja ko ru zh)

# ---------------------------------------------------------------------------
# Warning policy for first-party code.
#
# This project compiled with no warning flags at all, which is why a test
# section could be defined and never called (`-Wunused-function` is not on by
# default; it needs -Wall) and why the compiler had nothing to say about it.
# The policy is a function each first-party directory calls explicitly rather
# than a global add_compile_options, so that:
#   - the flag set has exactly one definition, and
#   - vendored subprojects (src/deps/pugixml) do not inherit our flags and get
#     linted as if the warnings were ours.
#
# Deliberately NOT -Werror: this codebase is built by several compilers, and
# promoting warnings to errors turns a compiler difference into a broken build.
# The point is to see the warnings, not to be blocked by them.
#
# MSVC is not enabled here. It IS a supported path (the Windows CI uses
# win64_msvc2022_64, whose equivalent level is /W4), but that path cannot be
# verified from this machine, and switching on a warning level blind would put
# unverified noise into CI. Left as a documented gap rather than a guess.
# ---------------------------------------------------------------------------
function(ae_enable_warnings)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-Wall -Wextra)
    endif()
endfunction()

# Get sources
add_subdirectory(src/util)
add_subdirectory(src/thr)
add_subdirectory(src/cmnd)
add_subdirectory(src/gl)
add_subdirectory(src/img)
add_subdirectory(src/core)
add_subdirectory(src/media)
add_subdirectory(src/ctrl)
add_subdirectory(src/gui)
add_subdirectory(src/deps/pugixml)
add_subdirectory(tests)

if(APPLE)
    set_target_properties( ${PROJECT_NAME}
            PROPERTIES
            MACOSX_BUNDLE_NAME                             ${PROJECT_NAME}
            MACOSX_BUNDLE_VERSION                          ${PROJECT_VERSION}
            MACOSX_BUNDLE_GUI_IDENTIFIER                   org.AnimeEffectsDevs.${PROJECT_NAME}
            MACOSX_BUNDLE_ICON_FILE                        AnimeEffects.icns
            MACOSX_BUNDLE                                  TRUE
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY             AnimeEffectsDevs
            XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED          NO
    )
endif ()