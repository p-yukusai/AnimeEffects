#!/usr/bin/env bash
# Re-creates tests/fixtures/e2e_stage.png, the project the end-to-end export harness
# (tests/media_exporter_e2e.cpp) runs against when it is given no argument.
#
# Why a committed fixture at all: that harness has 45 assertions, it needs a loaded
# project, and it was skipped in scripts/run_suites.sh with "needs a real .anie" - so
# the only time it ran was when a developer happened to remember. It accepted a project
# path but nothing in the repository could be loaded as one (the tests/golden PSDs are
# square, and the harness's exact-size scenario refuses to run on a square project
# because padding a square into a square proves nothing).
#
# The image is deliberately NOT square (96x64) and carries alpha, so the pad-or-crop
# path and the transparency path are both exercised. Two shapes, two colours: enough for
# the harness's content assertions (a blank render has to be distinguishable from art).
#
# ImageMagick rather than a checked-in generator program, exactly as the golden
# authoring script uses external reference tools: the fixture is data, this is the
# recipe, and re-running it must reproduce the file byte for byte. -strip is what makes
# that true - without it ImageMagick writes a date:create/date:modify text chunk and two
# runs of the same command differ (verified: two runs gave two different md5s).
set -euo pipefail
out="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/e2e_stage.png"
magick -size 96x64 xc:none \
    -fill '#e8503a' -draw 'circle 32,32 32,10' \
    -fill '#2f6fd0' -draw 'roundrectangle 56,12 88,52 6,6' \
    -define png:color-type=6 -strip PNG32:"$out"
echo "wrote $out"
