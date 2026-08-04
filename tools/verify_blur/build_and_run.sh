#!/usr/bin/env bash
# Builds the directional-blur verification harness against the ASan-instrumented
# libraries in build-asan/ and runs it (from the repo root, shaders load from ./data).
# Usage: tools/verify_blur/build_and_run.sh [--no-build]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

ASAN_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g"
QT_MODULES="Qt6Core Qt6Gui Qt6Widgets Qt6OpenGL Qt6OpenGLWidgets Qt6Xml Qt6Core5Compat Qt6Multimedia Qt6MultimediaWidgets"
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=0

if [[ "${1:-}" != "--no-build" ]]; then
    cmake --build build-asan --config Debug

    clang++ -std=c++17 -fPIC -O1 $ASAN_FLAGS \
        -DUSE_GL_CORE_PROFILE \
        -Isrc -Isrc/deps/pugixml/src \
        $(pkg-config --cflags $QT_MODULES) \
        tools/verify_blur/verify_blur.cpp tools/verify_blur/scene.cpp \
        -Wl,--start-group \
        build-asan/src/core/Debug/libcore.a \
        build-asan/src/ctrl/Debug/libctrl.a \
        build-asan/src/cmnd/Debug/libcmnd.a \
        build-asan/src/img/Debug/libimg.a \
        build-asan/src/gl/Debug/libgl.a \
        build-asan/src/thr/Debug/libthr.a \
        build-asan/src/util/Debug/libutil.a \
        build-asan/src/deps/pugixml/Debug/libpugixml.a \
        -Wl,--end-group \
        $(pkg-config --libs $QT_MODULES) -lGL \
        -o build-asan/verify_blur
fi

run() { # $1 = Qt platform
    echo "--- running with QT_QPA_PLATFORM=$1 ---"
    QT_QPA_PLATFORM="$1" ./build-asan/verify_blur
}

set +e
run offscreen
code=$?
if [[ $code -eq 2 ]]; then
    # exit code 2 = GL bootstrap failure; retry on the real display
    echo "offscreen GL unavailable, retrying on X11 (DISPLAY=${DISPLAY:-:0})"
    QT_QPA_PLATFORM=xcb DISPLAY="${DISPLAY:-:0}" ./build-asan/verify_blur
    code=$?
fi
exit $code
