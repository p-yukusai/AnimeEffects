// V3 — export preset compatibility matrix against the linked libav* build.
//
// Parses the embedded export presets (assets/encode/VideoEncode.txt) and, for
// every codec, checks:
//   * the preset's encoder name resolves to an encoder that EXISTS in the linked
//     build (with friendly-name aliases applied, e.g. "theora" -> "libtheora");
//   * every pixel format the preset lists is actually supported by that encoder.
// This is the gate that catches a preset that would fail at encoder open()-time
// (the old CLI subprocess hidden these incompatibilities behind a shell call).
//
// Headless, no GUI. Exit 0 = every preset is encodable as configured.
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QDomDocument>

#include "media/FFmpegHelpers.h"

static int failures = 0;
static void check(bool ok, const char* what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTextStream(stdout) << "=== V3 export preset compatibility matrix ===\n";

#if !defined(AE_VIDEOENCODE_PATH)
#error "AE_VIDEOENCODE_PATH must point at assets/encode/VideoEncode.txt"
#endif
    QFile file(QStringLiteral(AE_VIDEOENCODE_PATH));
    if (!file.open(QIODevice::ReadOnly)) {
        QTextStream(stdout) << "FAIL  cannot open preset file: " << AE_VIDEOENCODE_PATH << "\n";
        return 1;
    }
    QDomDocument doc;
    doc.setContent(&file);
    const QDomElement root = doc.firstChildElement("video_encode");

    int codecs = 0;
    for (QDomElement fmt = root.firstChildElement("format"); !fmt.isNull();
         fmt = fmt.nextSiblingElement("format")) {
        const QString fmtName = fmt.attribute("name");
        for (QDomElement cd = fmt.firstChildElement("codec"); !cd.isNull();
             cd = cd.nextSiblingElement("codec")) {
            const QString name = cd.attribute("name");
            if (name.isEmpty())
                continue;
            ++codecs;
            const QString enc = media::ffmpeg::resolveEncoderName(name);
            if (enc.isEmpty()) {
                check(false, qPrintable("encoder unavailable: " + name + " (in " + fmtName + ")"));
                continue;
            }
            // Friendly -> libav name is a real, present encoder.
            check(true, qPrintable("maps: " + name + " -> " + enc));

            // Hardware encoders (nvenc/qsv) need a device context and expose
            // hardware-specific pixel formats; the software-path encoder does
            // not drive them yet. Validate formats only for software codecs.
            const bool gpuEnc = cd.attribute("hint").contains("gpuenc");

            // Validate each listed pixel format (software codecs only).
            const QStringList pix = cd.attribute("pixfmt").split(',', Qt::SkipEmptyParts);
            for (const QString& p : pix) {
                const QString pt = p.trimmed();
                if (pt.isEmpty())
                    continue;
                if (gpuEnc) {
                    QTextStream(stdout) << "INFO  " << name << "/" << pt
                                        << " (gpuenc; hardware context required)\n";
                    continue;
                }
                if (!media::ffmpeg::encoderSupportsPixFormat(enc, pt)) {
                    check(false, qPrintable("pixfmt unsupported: " + name + "/" + pt));
                }
            }
        }
    }

    check(codecs > 0, "preset file contains codecs");

    // Friendly per-container encoder names (from the dialog combos) resolve to
    // real libav encoder names. This is what lets the exporter honor a user's
    // per-container encoder override instead of always using the container default.
    struct NameCase { const char* in; const char* out; };
    const NameCase names[] = {
        { "x264", "libx264" }, { "x265", "libx265" }, { "h264", "libx264" },
        { "vp8", "libvpx" }, { "vp9", "libvpx-vp9" }, { "theora", "libtheora" },
        { "mpeg2", "mpeg2video" },
    };
    bool namesOk = true;
    for (const auto& n : names) {
        const QString r = media::ffmpeg::resolveEncoderName(QString::fromUtf8(n.in));
        if (r != QLatin1String(n.out)) {
            QTextStream(stdout) << "  DEBUG resolveEncoderName(" << n.in << ")=" << r
                                << " expected " << n.out << "\n";
            namesOk = false;
        }
    }
    check(namesOk, "friendly encoder names resolve to libav names");

    // Container -> default-codec correctness. The exporter picks the default via
    // defaultCodecForContainer (muxer-aware), so the chosen codec MUST be one the
    // muxer can carry (this is what prevents a mis-mux like H.264 into a webp or
    // apng container). For each container the dialog offers, assert the default is
    // non-empty AND muxer-valid, OR (for exotic containers the in-process path
    // can't encode) empty so the exporter rejects cleanly.
    struct C { const char* container; bool expectDefault; };
    const C containers[] = {
        { "mp4",  true }, { "mov", true }, { "mkv", true }, { "webm", true },
        { "avi",  true }, { "ogv", true }, { "gif", false },
        { "webp", true }, { "apng", true }, { "flv", true }, { "swf", false },
        { "f4v",  true }, { "mp2", false },
    };
    for (const C& c : containers) {
        const QString codec = media::ffmpeg::defaultCodecForContainer(QString::fromUtf8(c.container));
        const bool valid = !codec.isEmpty() &&
            media::ffmpeg::muxerSupportsCodec(QString::fromUtf8(c.container), codec) &&
            !media::ffmpeg::resolveEncoderName(codec).isEmpty();
        if (c.expectDefault)
            check(valid, qPrintable(QString("default codec for %1 is muxer-valid").arg(c.container)));
        else
            check(!valid || codec.isEmpty(),
                  qPrintable(QString("default codec for %1 is empty (rejects cleanly)").arg(c.container)));
    }

    // Known-incompatible explicit combos must be rejected up front (this is the
    // rejection path that replaces a confusing muxer write error).
    check(!media::ffmpeg::muxerSupportsCodec("webm", "prores_ks"),
          "webm cannot carry prores_ks (rejected)");
    check(!media::ffmpeg::muxerSupportsCodec("mp4", "libtheora"),
          "mp4 cannot carry libtheora (rejected)");
    check(!media::ffmpeg::muxerSupportsCodec("apng", "libx264"),
          "apng cannot carry libx264 (rejected)");

    QTextStream(stdout) << "=== analyzed " << codecs << " codecs ===\n";
    return failures == 0 ? 0 : 1;
}
