#!/usr/bin/env bash
#
# Cross-compile AnimeEffects to a runnable Windows x86-64 build, then package a
# self-contained deployment (exe + Qt/minugw DLL closure + Qt plugins + data/
# + qt.conf).
#
# Requires (Fedora): mingw64-gcc mingw64-gcc-c++ mingw64-binutils \
#                    mingw64-qt6-{qtbase,qtsvg,qtmultimedia,qt5compat} \
#                    qt6-qtbase cmake ninja-build
#
# NOTE on the compiler: the top-level CMakeLists is normally configured with
# clang (CC/CXX are usually set to clang/clang++ for the native build). Fedora's
# mingw toolchain file only picks the cross compiler if CC/CXX are NOT set, so we
# MUST clear them (env -u CC -u CXX) or it will keep host clang and fail to link.
#
# Usage:
#   scripts/build-windows.sh                 # configure + build + deploy + zip
#   scripts/build-windows.sh --deploy-only   # skip build, assemble from an
#                                             # existing exe (in BUILD_DIR)
#   BUILD_DIR=xwin-build OUT_DIR=xwin-dist scripts/build-windows.sh --deploy-only
#
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$SRC_DIR/build-win}"
OUT_DIR="${OUT_DIR:-$SRC_DIR/xwin-dist}"
ZIP="${ZIP:-$SRC_DIR/AnimeEffects-windows-x86_64.zip}"

SYSROOT="/usr/x86_64-w64-mingw32/sys-root/mingw"
TOOLCHAIN="/usr/share/mingw/toolchain-mingw64.cmake"
OBJDUMP="x86_64-w64-mingw32-objdump"

deploy_only=0
for a in "$@"; do
  case "$a" in --deploy-only) deploy_only=1 ;; esac
done

die() { echo "ERROR: $*" >&2; exit 1; }

[ -f "$TOOLCHAIN" ] || die "$TOOLCHAIN not found - install mingw64-* (see header)"
[ -f "$SYSROOT/lib/cmake/Qt6/Qt6Config.cmake" ] || die "Windows Qt6 not found in $SYSROOT - install mingw64-qt6-*"
command -v "$OBJDUMP" >/dev/null || die "$OBJDUMP not found - install mingw64-binutils"

# --- 1. configure + build -----------------------------------------------------
if [ "$deploy_only" -eq 0 ]; then
  echo "== configure =="
  rm -rf "$BUILD_DIR"
  env -u CC -u CXX cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH=/usr \
    -DCMAKE_PREFIX_PATH="$SYSROOT" \
    || die "configure failed"

  echo "== build =="
  env -u CC -u CXX cmake --build "$BUILD_DIR" --target AnimeEffects -j "$(nproc)" \
    || die "build failed"
fi

EXE="$BUILD_DIR/src/gui/AnimeEffects.exe"
[ -f "$EXE" ] || die "no exe at $EXE (run without --deploy-only, or set BUILD_DIR)"
echo "== exe: $EXE =="

# --- 2. assemble deployment ---------------------------------------------------
echo "== assembling deployment -> $OUT_DIR =="
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR/plugins"
cp "$EXE" "$OUT_DIR/"
cp -r "$SRC_DIR/data" "$OUT_DIR/data"
# Qt plugins live under lib/qt6/plugins in the sysroot; copy the whole tree so the
# platform (qwindows), image formats, styles, multimedia, icon engines, etc. ship.
cp -r "$SYSROOT/lib/qt6/plugins/." "$OUT_DIR/plugins/"
# Without qt.conf Qt looks at its compile-time prefix (a Linux mingw path) for
# plugins; [Paths] Plugins=plugins points it at the deployed <exe-dir>/plugins.
printf '[Paths]\r\nPlugins=plugins\r\n' > "$OUT_DIR/qt.conf"

# Recursively copy every non-system DLL the exe and the plugins import, resolved
# against the sysroot (DLL import libs are lazily pulled, so walk the closure).
# Precompute a basename->path map so we don't scan the whole sysroot per import.
declare -A DLLMAP
while IFS= read -r p; do DLLMAP[$(basename "$p" | tr 'A-Z' 'a-z')]="$p"; done < <(find "$SYSROOT" -iname '*.dll' 2>/dev/null)
queue=("$EXE")
while IFS= read -r f; do queue+=("$f"); done < <(find "$OUT_DIR/plugins" -iname '*.dll')
i=0
while [ "$i" -lt "${#queue[@]}" ]; do
  f="${queue[$i]}"; i=$((i+1))
  while IFS= read -r dll; do
    src="${DLLMAP[$(echo "$dll" | tr 'A-Z' 'a-z')]-}"
    if [ -n "$src" ] && [ ! -e "$OUT_DIR/$dll" ]; then
      cp "$src" "$OUT_DIR/$dll"; queue+=("$OUT_DIR/$dll")
    fi
  done < <("$OBJDUMP" -p "$f" 2>/dev/null | grep -i 'DLL Name' | sed 's/.*DLL Name: //' | tr -d '\r')
done
echo "   DLLs in deployment: $(find "$OUT_DIR" -iname '*.dll' | wc -l)"

# --- 3. package -----------------------------------------------------------
echo "== packaging -> $ZIP =="
rm -f "$ZIP"
( cd "$(dirname "$OUT_DIR")" && zip -qr "$ZIP" "$(basename "$OUT_DIR")" ) || die "zip failed"
echo ""
echo "DONE."
echo "  exe:        $EXE"
echo "  deployment: $OUT_DIR/   (AnimeEffects.exe + data/ + plugins/ + qt.conf + DLLs)"
echo "  archive:    $ZIP"
echo ""
echo "Run on Windows: copy the folder (or unzip), then double-click AnimeEffects.exe."
echo "Smart App Control will block this unsigned build; disable it (Windows Security"
echo "-> App & browser control -> Smart App Control -> Off) or sign the binaries."
