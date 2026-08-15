cmake_minimum_required(VERSION 3.16)
project(AnimeEffects VERSION 1.7.1 LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# ASan on debug builds when needed (pretty bad perf hit)
# set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address")
# set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fsanitize=address")

include(GNUInstallDirs)
# You need to add the Qt directory to your path, otherwise you'll have a very bad time
# On MacOS Tahoe please remove AGL from the framework list when linking
find_package(Qt6 REQUIRED COMPONENTS Core5Compat Core Gui OpenGL OpenGLWidgets Svg Xml Concurrent Widgets Multimedia)
# Get sources
add_subdirectory(src/util)
add_subdirectory(src/thr)
add_subdirectory(src/cmnd)
add_subdirectory(src/gl)
add_subdirectory(src/img)
add_subdirectory(src/core)
add_subdirectory(src/ctrl)
add_subdirectory(src/gui)
add_subdirectory(src/deps/pugixml)

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