#!/usr/bin/env sh
# Run an ASan-instrumented AnimeEffects binary with LeakSanitizer
# suppressions for known third-party process-lifetime caches
# (fontconfig/Pango/GTK3 via the Qt gtk3 platform theme).
#
# Usage: scripts/run_asan.sh [path/to/AnimeEffects]
# Default binary: build-asan/src/gui/Debug/AnimeEffects
set -e

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BIN="${1:-$SCRIPT_DIR/../build-asan/src/gui/Debug/AnimeEffects}"
SUPP="$SCRIPT_DIR/../tools/lsan.supp"

export LSAN_OPTIONS="suppressions=${SUPP}${LSAN_OPTIONS:+:$LSAN_OPTIONS}"

exec "$BIN"
