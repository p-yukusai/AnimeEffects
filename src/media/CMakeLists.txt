project(anime_media)
ae_enable_warnings()

# The media library owns all FFmpeg linkage. It is deliberately GL-free (and
# mostly QCoreApplication-free) so it can be unit-tested headless; it only
# needs Qt Core for QString/QStringList.
find_package(Qt6 REQUIRED COMPONENTS Core Gui)

# ---------------------------------------------------------------------------
# FFmpeg discovery: pkg-config on the Unixes, an SDK directory everywhere else.
#
# pkg-config was the only route here, and REQUIRING it broke the build on
# Windows before a single source file was compiled: pkg-config is not part of
# MSVC, Visual Studio or a stock Windows box. Windows FFmpeg distributions
# (BtbN/FFmpeg-Builds' win64-gpl-shared archives, gyan.dev's shared builds,
# vcpkg) ship headers and .lib import libraries in an include/ lib/ bin/
# layout instead, so that layout is a first-class route below - not a
# workaround bolted onto the Unix path.
#
# Whichever route answers, it produces the same five imported targets -
# FFmpeg::avcodec, FFmpeg::avformat, FFmpeg::avutil, FFmpeg::swscale and
# FFmpeg::swresample - because everything downstream links those, never a
# route. An explicit AE_FFMPEG_ROOT wins over pkg-config, so a hand-built GPL
# SDK can override a distro package:
#
#   1. AE_FFMPEG_ROOT   an SDK laid out as include/ lib/ bin/
#   2. pkg-config       libavformat/libavutil/libswscale/libswresample/libavcodec
#   3. find_path/find_library  the same five, from CMAKE_PREFIX_PATH or the
#                              system (a Windows SDK on PATH, vcpkg, ...)
# ---------------------------------------------------------------------------
set(AE_FFMPEG_ROOT "" CACHE PATH
    "Root of an FFmpeg shared build (include/, lib/, bin/). Empty = pkg-config / find_library.")

set(_ae_ffmpeg_modules avcodec avformat avutil swscale swresample)
set(_ae_ffmpeg_route "")
set(_ae_ffmpeg_include_dirs "")

# find_path/find_library cache their answers. A later configure with a different
# AE_FFMPEG_ROOT must search again, or the build silently keeps linking the old
# SDK while the new root is displayed as the source (the cache is keyed by the
# variable name, not by the hints).
unset(_ae_ffmpeg_include_dir CACHE)
unset(_ae_ffmpeg_include_dir)
foreach(_mod IN LISTS _ae_ffmpeg_modules)
    unset(_ae_ffmpeg_${_mod}_library CACHE)
    unset(_ae_ffmpeg_${_mod}_library)
endforeach()

# --- 1. an explicit SDK root ------------------------------------------------
if(AE_FFMPEG_ROOT)
    find_path(_ae_ffmpeg_include_dir
        NAMES libavcodec/avcodec.h
        HINTS "${AE_FFMPEG_ROOT}"
        PATH_SUFFIXES include
        NO_DEFAULT_PATH)
    if(NOT _ae_ffmpeg_include_dir)
        message(FATAL_ERROR
            "AE_FFMPEG_ROOT='${AE_FFMPEG_ROOT}' has no include/libavcodec/avcodec.h. "
            "Point it at the ROOT of an FFmpeg shared build (the directory that holds "
            "include/, lib/ and bin/).")
    endif()
    foreach(_mod IN LISTS _ae_ffmpeg_modules)
        find_library(_ae_ffmpeg_${_mod}_library
            NAMES ${_mod}
            HINTS "${AE_FFMPEG_ROOT}"
            PATH_SUFFIXES lib
            NO_DEFAULT_PATH)
        if(NOT _ae_ffmpeg_${_mod}_library)
            message(FATAL_ERROR
                "AE_FFMPEG_ROOT='${AE_FFMPEG_ROOT}' has no ${_mod} library under lib/. "
                "A bin-only FFmpeg download (ffmpeg.exe/ffplay.exe) cannot be linked "
                "against; take a SHARED build, which also carries include/ and lib/.")
        endif()
    endforeach()
    set(_ae_ffmpeg_include_dirs "${_ae_ffmpeg_include_dir}")
    set(_ae_ffmpeg_route "sdk")
endif()

# --- 2. pkg-config (the Unix default) ---------------------------------------
if(NOT _ae_ffmpeg_route)
    find_package(PkgConfig QUIET)
endif()
if(NOT _ae_ffmpeg_route AND PkgConfig_FOUND)
    pkg_check_modules(AE_PC_AVFORMAT QUIET IMPORTED_TARGET libavformat)
    pkg_check_modules(AE_PC_AVUTIL   QUIET IMPORTED_TARGET libavutil)
    pkg_check_modules(AE_PC_SWSCALE  QUIET IMPORTED_TARGET libswscale)
    pkg_check_modules(AE_PC_SWRESAMPLE QUIET IMPORTED_TARGET libswresample)
    pkg_check_modules(AE_PC_AVCODEC  QUIET IMPORTED_TARGET libavcodec)
    if(AE_PC_AVFORMAT_FOUND AND AE_PC_AVUTIL_FOUND AND AE_PC_SWSCALE_FOUND
       AND AE_PC_SWRESAMPLE_FOUND AND AE_PC_AVCODEC_FOUND)
        set(_ae_ffmpeg_include_dirs
            ${AE_PC_AVCODEC_INCLUDE_DIRS} ${AE_PC_AVFORMAT_INCLUDE_DIRS}
            ${AE_PC_AVUTIL_INCLUDE_DIRS}  ${AE_PC_SWSCALE_INCLUDE_DIRS}
            ${AE_PC_SWRESAMPLE_INCLUDE_DIRS})
        list(REMOVE_DUPLICATES _ae_ffmpeg_include_dirs)
        set(_ae_ffmpeg_route "pkg-config")
    endif()
endif()

# --- 3. plain find_path/find_library ----------------------------------------
if(NOT _ae_ffmpeg_route)
    find_path(_ae_ffmpeg_include_dir NAMES libavcodec/avcodec.h)
    if(_ae_ffmpeg_include_dir)
        set(_ae_ffmpeg_all_found TRUE)
        foreach(_mod IN LISTS _ae_ffmpeg_modules)
            find_library(_ae_ffmpeg_${_mod}_library NAMES ${_mod})
            if(NOT _ae_ffmpeg_${_mod}_library)
                set(_ae_ffmpeg_all_found FALSE)
            endif()
        endforeach()
        if(_ae_ffmpeg_all_found)
            set(_ae_ffmpeg_include_dirs "${_ae_ffmpeg_include_dir}")
            set(_ae_ffmpeg_route "find")
        endif()
    endif()
endif()

if(NOT _ae_ffmpeg_route)
    message(FATAL_ERROR
        "FFmpeg (libav*) was not found. On Windows pass "
        "-DAE_FFMPEG_ROOT=<the root of an FFmpeg SHARED build, i.e. the directory "
        "holding include/, lib/ and bin/>. On Unix install pkg-config plus the "
        "libavcodec/libavformat/libavutil/libswscale/libswresample development "
        "packages.")
endif()
message(STATUS "media: FFmpeg via ${_ae_ffmpeg_route}")

# The GPL encoder support override. On systems where the discovered libavcodec
# is the LGPL "free" build, it does NOT contain the GPL x264/x265 encoders that
# the H.264/H.265 export presets require. Point AE_FFMPEG_GPL_AVCODEC at a GPL
# libavcodec (on Fedora /usr/lib64/ffmpeg/libavcodec.so; on Windows the .lib of
# a GPL SDK) and it is linked in place of the discovered one. Empty = use the
# discovered libavcodec.
set(AE_FFMPEG_GPL_AVCODEC "" CACHE FILEPATH
    "Path to a GPL libavcodec (.so/.lib, with libx264/libx265). Empty = the discovered libavcodec.")

if(AE_FFMPEG_GPL_AVCODEC AND NOT EXISTS "${AE_FFMPEG_GPL_AVCODEC}")
    message(FATAL_ERROR
        "AE_FFMPEG_GPL_AVCODEC='${AE_FFMPEG_GPL_AVCODEC}' does not exist.")
endif()

# One target per module, whichever route answered, so callers never branch on
# the route. A pkg-config module keeps its own imported target as the link
# interface (it already carries the include dirs and any dependent libs); the
# other routes link the absolute library file.
foreach(_mod IN LISTS _ae_ffmpeg_modules)
    if(_mod STREQUAL "avcodec" AND AE_FFMPEG_GPL_AVCODEC)
        # Created below, from the override.
        continue()
    endif()
    string(TOUPPER "${_mod}" _MOD)
    if(_ae_ffmpeg_route STREQUAL "pkg-config")
        add_library(FFmpeg::${_mod} INTERFACE IMPORTED)
        set_target_properties(FFmpeg::${_mod} PROPERTIES
            INTERFACE_LINK_LIBRARIES "PkgConfig::AE_PC_${_MOD}")
    else()
        add_library(FFmpeg::${_mod} UNKNOWN IMPORTED)
        set_target_properties(FFmpeg::${_mod} PROPERTIES
            IMPORTED_LOCATION "${_ae_ffmpeg_${_mod}_library}"
            INTERFACE_INCLUDE_DIRECTORIES "${_ae_ffmpeg_include_dirs}")
    endif()
endforeach()

# The override replaces the avcodec target. IMPORTED_NO_SONAME is what lets a
# bare libavcodec.so (no SONAME) link.
if(AE_FFMPEG_GPL_AVCODEC)
    add_library(FFmpeg::avcodec UNKNOWN IMPORTED)
    set_target_properties(FFmpeg::avcodec PROPERTIES
        IMPORTED_LOCATION "${AE_FFMPEG_GPL_AVCODEC}"
        INTERFACE_INCLUDE_DIRECTORIES "${_ae_ffmpeg_include_dirs}"
        IMPORTED_NO_SONAME TRUE
    )
    message(STATUS "media: linking GPL libavcodec ${AE_FFMPEG_GPL_AVCODEC} "
                   "(libx264/libx265 available)")
else()
    message(STATUS "media: linking the discovered libavcodec "
                   "(default/LGPL; libx264/libx265 may be ABSENT)")
endif()

# ---------------------------------------------------------------------------
# The runtime DLLs of an SDK.
#
# The import libraries above are enough to LINK, but a shared FFmpeg is loaded
# when the process starts: without avcodec-*.dll next to the exe (or on PATH)
# the application dies with 0xc0000135 before main(). windeployqt only knows
# Qt, so the exact set is resolved here once and exported to the parent scope;
# src/gui copies it next to the build-tree binary and the packaging scripts
# assemble their deployment from the same idea.
#
# Only the SDK root can name them reliably: the pkg-config and find_library
# routes may point at a system lib directory with no bin/ beside it, and
# several FFmpeg installations can be on PATH at once. On those routes the
# DLLs are the environment's business (a distro package, or vcpkg's
# app-local deployment).
# ---------------------------------------------------------------------------
set(AE_FFMPEG_RUNTIME_DLLS "")
if(WIN32 AND _ae_ffmpeg_route STREQUAL "sdk")
    foreach(_mod IN LISTS _ae_ffmpeg_modules)
        file(GLOB _ae_ffmpeg_dlls
            "${AE_FFMPEG_ROOT}/bin/${_mod}-*.dll"
            "${AE_FFMPEG_ROOT}/bin/${_mod}.dll")
        if(NOT _ae_ffmpeg_dlls)
            message(WARNING
                "AE_FFMPEG_ROOT='${AE_FFMPEG_ROOT}' has no ${_mod} DLL under bin/. "
                "A build-tree launch of AnimeEffects will need bin/ on PATH.")
        endif()
        list(APPEND AE_FFMPEG_RUNTIME_DLLS ${_ae_ffmpeg_dlls})
    endforeach()
    list(REMOVE_DUPLICATES AE_FFMPEG_RUNTIME_DLLS)
    list(LENGTH AE_FFMPEG_RUNTIME_DLLS _ae_ffmpeg_dll_count)
    message(STATUS "media: ${_ae_ffmpeg_dll_count} FFmpeg runtime DLLs will be copied next to the exe")
endif()
set(AE_FFMPEG_RUNTIME_DLLS "${AE_FFMPEG_RUNTIME_DLLS}" PARENT_SCOPE)

# The default export presets (mp4/mov/mkv/vp9...) rely on the GPL x264/x265
# encoders. Make the codec set EXPLICIT so a build can't silently degrade (e.g.
# mp4 -> mpeg4 when x264 is absent). Either point AE_FFMPEG_GPL_AVCODEC at a GPL
# libavcodec, or explicitly accept the reduced free-only codec set.
option(AE_ALLOW_FREE_CODECS_ONLY "Build with the free/LGPL codec set only (no libx264/libx265)" OFF)
if(NOT AE_ALLOW_FREE_CODECS_ONLY AND NOT AE_FFMPEG_GPL_AVCODEC)
    message(FATAL_ERROR
        "The default H.264/H.265 export presets need a GPL FFmpeg build "
        "(libx264/libx265). Set AE_FFMPEG_GPL_AVCODEC=/path/to/GPL/libavcodec "
        "for the full codec set, or pass -DAE_ALLOW_FREE_CODECS_ONLY=ON to accept "
        "the reduced free/LGPL codec set (mp4 falls back to mpeg4, no x264/x265).")
endif()

add_library(media STATIC
    FFmpegHelpers.cpp
    FFmpegHelpers.h
    VideoEncoder.cpp
    VideoEncoder.h
    AudioMixer.cpp
    AudioMixer.h
    GifEncoder.cpp
    GifEncoder.h
)
target_include_directories(media PUBLIC
    # Resolve "media/FFmpegHelpers.h" the same way the app resolves
    # "ctrl/Exporter.h": from src/.
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(media PUBLIC
    Qt6::Core
    Qt6::Gui
    FFmpeg::avformat FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample
    FFmpeg::avcodec
)
