// Golden-image regression testing: code-generated import fixtures (PSD/ORA) with
// externally authored reference renders.
//
// Layout: tests/golden/<case>/{case.json, input.psd|ora, expected.png}
//   - generateInputs() writes case.json (first time only) + input.* for every compiled-in
//     case; inputs are deterministic, so regeneration is reviewable in git diffs.
//   - expected.png is authored OUTSIDE this tree (tests/golden/author_goldens.sh exports
//     through flatpak Krita or GIMP, per case.json's "author") and is never touched by
//     the generator.
//   - suite() (S14) imports each input through the production ImageFileLoader, renders
//     frame 0 through the production composite wiring and diffs against expected.png
//     with per-case tolerances from case.json. Cases without expected.png are skipped
//     (reported, not failed).
#ifndef VERIFY_BLUR_GOLDEN_H
#define VERIFY_BLUR_GOLDEN_H

#include <QString>

namespace golden {

// writes tests/golden/<case>/{case.json,input.*}; returns a process exit code
int generateInputs(const QString& aRootDir);

// S14 suite: import -> render -> diff vs expected.png for every runnable case;
// when aOnlyCase is non-empty only that subdirectory runs (per-file isolation)
void suite(const QString& aRootDir, const QString& aOnlyCase = QString());

} // namespace golden

#endif // VERIFY_BLUR_GOLDEN_H
