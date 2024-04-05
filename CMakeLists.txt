cmake_minimum_required(VERSION 3.16)
project(AnimeEffects VERSION 1.5.0 LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)
# You need to add the Qt directory to your path if you're on Windows, otherwise you'll have a very bad time
find_package(Qt6 REQUIRED COMPONENTS Core5Compat Core Gui OpenGL OpenGLWidgets Xml Concurrent Widgets)
# Get sources
add_subdirectory(src/util)
add_subdirectory(src/thr)
add_subdirectory(src/cmnd)
add_subdirectory(src/gl)
add_subdirectory(src/img)
add_subdirectory(src/core)
add_subdirectory(src/ctrl)
add_subdirectory(src/gui)
