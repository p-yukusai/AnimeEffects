#!/usr/bin/env bash
# Re-exports every tests/golden/<case>/input.* to expected.png via a reference program,
# headless. The exported PNG is the external ground truth the S14 golden suite diffs
# against. Safe to re-run (overwrites every golden); only needed when cases are added,
# inputs change, or the reference renderers are deliberately upgraded.
#
# Each case.json picks its author ("author": "krita" | "gimp"; default krita):
#   krita - flatpak org.kde.krita --export. Fast, strong PSD/ORA import, but IGNORES the
#           opacity byte on PSD group records (probe-verified on 5.3: op=0 renders full
#           strength), so fractional-opacity PSD groups must not be Krita-authored.
#   gimp  - flatpak org.gimp.GIMP batch script-fu. Honors PSD group opacity (lerps like
#           AE). Used for the fractional-opacity PSD group cases.
#
# Notes:
#   - QT_QPA_PLATFORM=wayland is the Krita default because offscreen/xcb crash inside
#     this flatpak sandbox on some machines; override with QT_QPA_PLATFORM=<platform>.
#   - in/out paths must be host-visible (flatpak /tmp is sandbox-private), which the
#     tests/golden tree is.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QPA="${QT_QPA_PLATFORM:-wayland}"
failed=0
for dir in "$ROOT"/*/; do
    [[ -f "$dir/case.json" ]] || continue
    author="$(python3 -c "import json; print(json.load(open('$dir/case.json')).get('author', 'krita'))")"
    for input in "$dir"/input.psd "$dir"/input.ora; do
        [[ -f "$input" ]] || continue
        echo "$author: ${input#$ROOT/}"
        case "$author" in
        gimp)
            timeout 240 flatpak run org.gimp.GIMP -i --batch-interpreter=plug-in-script-fu-eval \
                -b "(let* ((image (car (gimp-file-load RUN-NONINTERACTIVE \"$input\" \"input\")))) (gimp-image-flatten image) (gimp-file-save RUN-NONINTERACTIVE image \"$dir/expected.png\" \"expected.png\") (gimp-image-delete image))" \
                -b "(gimp-quit 0)" >/dev/null 2>&1 ;;
        *)
            flatpak run --env=QT_QPA_PLATFORM="$QPA" org.kde.krita \
                --export --export-filename "$dir/expected.png" "$input" >/dev/null 2>&1 ;;
        esac
        if [[ ! -s "$dir/expected.png" ]]; then
            echo "  FAILED: $input" >&2
            failed=1
        fi
    done
done
if [[ $failed -eq 0 ]]; then
    echo "all goldens exported. Diff with: tools/verify_blur/build_and_run.sh"
else
    echo "some exports failed (see above)" >&2
fi
exit $failed
