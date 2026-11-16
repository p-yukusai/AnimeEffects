// One temp directory per test PROCESS. Use aeTestTempPath() for every path a
// test writes.
//
// Why this exists: the media suites wrote FIXED names into the shared system temp
// dir ("ae_e2e.mp4", "ae_e2e_frames", "ae_fit_cost_telegram_sticker.webm", ...),
// so two runs at the same time wrote, stat'ed and QFile::remove()d each other's
// files. Measured on media_exporter_e2e: SIX of six concurrent runs failed, and
// not only on the assertions that read a size. One process's ffprobe measured
// -100 dB and "0 bytes" because the file it was handed had been deleted or
// replaced by the other process moments earlier, so the harness reported "the
// artwork is not in the frame" and "no file produced" about a file that was never
// its own. Those readings were of the harness, not of the exporters, and the
// failures looked like one-off flakes because running the suite once at a time
// cannot see them.
//
// Running a suite twice at once is ordinary: Debug and Release side by side,
// ctest -j, or two developers on one machine. A gate that fails then is worse than
// no gate, because it reports the code for the harness's own collision. Per-process
// names make the suites safe to run in parallel, and QTemporaryDir removes the
// directory - with everything in it - when the process exits, so a killed run no
// longer leaves its output in /tmp to be misread by the next one either.
#ifndef AE_TESTS_TEMP_UTIL_H
#define AE_TESTS_TEMP_UTIL_H

#include <QDir>
#include <QString>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

// Path of aName inside this process's own temp directory. The directory is created
// on first use and removed at exit.
inline QString aeTestTempPath(const QString& aName)
{
    // Absolute template: a RELATIVE one is resolved against the current working
    // directory, not the temp dir (QTemporaryDir's docs), which would litter the
    // source tree. XXXXXX at the END is the part made unique; it is not supported
    // in the middle of the template.
    static QTemporaryDir dir(QDir::temp().filePath(QStringLiteral("ae_test_XXXXXX")));
    if (!dir.isValid()) {
        // Not a test failure: with no writable temp dir there is nothing to assert
        // about, so say which it is rather than failing every write below it.
        std::fprintf(stderr, "no writable temp dir for this test: %s\n",
                     qPrintable(dir.errorString()));
        std::exit(2);
    }
    return dir.filePath(aName);
}

#endif // AE_TESTS_TEMP_UTIL_H
