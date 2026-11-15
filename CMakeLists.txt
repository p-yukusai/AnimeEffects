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
find_package(Qt6 REQUIRED COMPONENTS Core5Compat Core Gui OpenGL OpenGLWidgets Svg Xml Concurrent Widgets Multimedia Quick Qml QuickWidgets LinguistTools)
# Qt's i18n CMake support: declares the translated languages lupdate/lrelease
# operate on, and enables the qt_add_translations/update_translations targets.
# The translation sources live in data/locale/translation_<locale>.ts.
qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES es ja ko ru zh)
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