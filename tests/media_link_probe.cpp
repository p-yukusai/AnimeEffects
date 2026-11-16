// V0 — build/link gate for the media (libav*) rearchitecture.
//
// Verifies that the app links FFmpeg as a LIBRARY (not the CLI subprocess):
//   * all five libav* modules are linked (compile + link succeed),
//   * the linked libavcodec reports a license + configuration,
//   * at least one video encoder is registered (proves libavcodec works),
//   * reports whether GPL encoders (libx264/libx265) are present, which gates
//     which export presets are usable with the linked FFmpeg build.
//
// Headless, no QApplication, no GL. Exit 0 = linkage verified.
#include <QCoreApplication>
#include <QTextStream>
#include <QStringList>

#include "media/FFmpegHelpers.h"

static int failures = 0;

static void check(bool ok, const char* what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    const QString license = media::ffmpeg::license();
    const QString config = media::ffmpeg::configuration();

    out << "=== libav* license ===\n" << license << "\n";
    out << "=== configure flags (truncated) ===\n"
        << config.left(600) << (config.size() > 600 ? "\n...(truncated)" : "") << "\n";

    check(!license.isEmpty(), "libavcodec license() is non-empty (lib linked)");
    check(media::ffmpeg::encoderAvailable("png"),
          "png encoder available (libavcodec is functional)");

    const QStringList encoders = media::ffmpeg::availableVideoEncoders();
    out << "=== registered video encoders (" << encoders.size() << ") ===\n"
        << encoders.join(", ") << "\n";
    check(!encoders.isEmpty(), "at least one video encoder is registered");

    // GPL build indicator: these are the GPL-only encoders used by the H.264/
    // H.265 export presets. If absent, the linked libavcodec is the LGPL "free"
    // build and those presets cannot be encoded with it.
    const bool gpl264 = media::ffmpeg::encoderAvailable("libx264");
    const bool gpl265 = media::ffmpeg::encoderAvailable("libx265");
    out << "=== GPL encoders ===\n"
        << "  libx264: " << (gpl264 ? "present" : "ABSENT") << "\n"
        << "  libx265: " << (gpl265 ? "present" : "ABSENT") << "\n";

    if (gpl264 || gpl265)
        out << "=> linked a GPL libavcodec (x26x available).\n";
    else
        out << "=> linked an LGPL/\'free\' libavcodec (no x26x). Set "
               "AE_FFMPEG_GPL_AVCODEC to a GPL build to enable x26x.\n";

    check(gpl264 && gpl265, "libx264 + libx265 are available (GPL FFmpeg, per chosen build)");

    out << "=== V0 result ===\n";
    return failures == 0 ? 0 : 1;
}
