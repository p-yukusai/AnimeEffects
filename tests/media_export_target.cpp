// Export-destination table prototype (DESIGN RESEARCH).
//
// This is the gate for the "destination-driven export" design: the table in
// assets/export/ExportTargets.txt is DATA, and this test proves three things
// about it.
//
//   1. Every row is well-formed and its codec actually exists in the linked
//      libav* build (and its muxer can carry it).
//   2. Constraint application (size rules, fps/duration/frame caps) does what
//      the destination's published limits require.
//   3. A resolved plan actually encodes — the loop is closed from data to a
//      real, decodable file.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/media_export_target
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

#include "ctrl/ExportTarget.h"
#include "ctrl/ExportTargetStrings.h"
#include "decode_util.h"
#include "temp_util.h"
#include "media/FFmpegHelpers.h"
#include "media/GifEncoder.h"
#include "media/VideoEncoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <QVector>
#include <algorithm>
#include <cmath>
#include <climits>

// ---------------------------------------------------------------------------
// Section registration.
//
// Sections used to be listed by hand in main(), and run_live_encode() was
// written, read as coverage, and never listed — so six destinations' worth of
// encoding assertions silently contributed nothing. Adding the missing call
// fixes the instance; it does not fix the class, because the next section could
// be forgotten the same way.
//
// So DEFINING a section is now what makes it run. The registrar appends itself
// at static-init time and main() walks the registry, which makes
// "defined but never executed" unrepresentable rather than merely discouraged.
// The title is part of the macro so a section cannot forget to announce itself
// either — the run banner and the section are one declaration.
//
// The registry is heap-allocated and deliberately never freed: a static QVector
// would be destroyed in an order we do not control, while registrars hold
// pointers to it.
namespace {

struct TestSection {
    const char* title;
    void (*fn)();
};

QVector<TestSection>& testSections() {
    static QVector<TestSection>* registry = new QVector<TestSection>();
    return *registry;
}

struct TestSectionRegistrar {
    TestSectionRegistrar(const char* aTitle, void (*aFn)()) {
        testSections().append(TestSection{aTitle, aFn});
    }
};

} // namespace

#define TEST_SECTION(title, fn)                              \
    static void fn();                                        \
    static TestSectionRegistrar fn##_registrar(title, &fn);  \
    static void fn()

static int failures = 0;

static void check(bool ok, const char* what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

static void check(bool ok, const QString& what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// Deterministic synthetic RGBA frame: gradient + alpha ramp, so both luma and
// chroma vary and alpha is exercised when the target carries it.
static QImage makeFrame(int w, int h, int idx) {
    QImage img(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            line[4 * x + 0] = uchar((x * 255) / (w == 0 ? 1 : w));
            line[4 * x + 1] = uchar((y * 255) / (h == 0 ? 1 : h));
            line[4 * x + 2] = uchar(((x + y + idx) * 255) / ((w + h) == 0 ? 1 : (w + h)));
            line[4 * x + 3] = uchar(255 - ((x + y) % 256));
        }
    }
    return img;
}

// Incompressible, deterministic RGBA frame. File size is unpredictable in
// advance, which is the reason a byte cap needs a measure-and-retry loop; noise
// makes the FIRST attempt reliably over a small cap, so a test that depends on
// the loop actually degrading cannot silently pass on a lucky first encode.
static QImage makeNoiseFrame(int w, int h, int idx) {
    QImage img(w, h, QImage::Format_RGBA8888);
    quint32 s = 0x9e3779b9u ^ quint32(idx * 2654435761u);
    for (int y = 0; y < h; ++y) {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;   // xorshift32
            line[4 * x + 0] = uchar(s & 0xff);
            line[4 * x + 1] = uchar((s >> 8) & 0xff);
            line[4 * x + 2] = uchar((s >> 16) & 0xff);
            line[4 * x + 3] = 255;
        }
    }
    return img;
}

// How many frames a GIF actually contains. A cap can be met by writing a
// different animation than the plan describes (the stride is a frame count, not
// a size), so size alone does not prove the artifact is the one promised.
static int gifFrameCount(const QString& aPath) {
    AVFormatContext* fmt = nullptr;
    if (::avformat_open_input(&fmt, aPath.toUtf8().constData(), nullptr, nullptr) < 0) return -1;
    ::avformat_find_stream_info(fmt, nullptr);
    const int vs = ::av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vs < 0) { ::avformat_close_input(&fmt); return -1; }
    const AVCodec* dec = ::avcodec_find_decoder(fmt->streams[vs]->codecpar->codec_id);
    AVCodecContext* ctx = ::avcodec_alloc_context3(dec);
    ::avcodec_parameters_to_context(ctx, fmt->streams[vs]->codecpar);
    if (::avcodec_open2(ctx, dec, nullptr) < 0) {
        ::avcodec_free_context(&ctx);
        ::avformat_close_input(&fmt);
        return -1;
    }
    AVFrame* frame = ::av_frame_alloc();
    AVPacket* pkt = ::av_packet_alloc();
    int frames = 0;
    while (::av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vs) {
            ::avcodec_send_packet(ctx, pkt);
            while (::avcodec_receive_frame(ctx, frame) >= 0) ++frames;
        }
        ::av_packet_unref(pkt);
    }
    ::avcodec_send_packet(ctx, nullptr);
    while (::avcodec_receive_frame(ctx, frame) >= 0) ++frames;
    ::av_frame_free(&frame);
    ::av_packet_free(&pkt);
    ::avcodec_free_context(&ctx);
    ::avformat_close_input(&fmt);
    return frames;
}

// Everything we can learn about a produced file that a destination actually
// cares about: dimensions, frame count, the container's stream codec tag, how
// many audio streams exist, and whether the decoded alpha channel varies.
struct Probe {
    bool opened = false;
    int frames = 0;
    int width = 0;
    int height = 0;
    uint32_t codecTag = 0;
    int audioStreams = 0;
    bool alphaDecoded = false;
    int minAlpha = 255;
    int maxAlpha = 0;
    QString decodedPixFmt; // what the DECODER produced (not the container's tag)
};

// `aDecoderName` forces a specific decoder. This matters: the native FFmpeg VP9
// decoder DISCARDS the alpha plane, so VP9+alpha can only be observed through
// the libvpx decoder.
// What the destination rows actually require of a WebP, read from the container.
//
// FFmpeg cannot decode ANIMATED WebP at all — its demuxer handles stills and
// reports "image data not found" for the rest — so the WebP destinations had no
// decode assertions whatsoever. That is a real gap: Discord emoji and WhatsApp
// stickers are WebP-only, and "we cannot read it back" is not the same as "it is
// correct". The container is a documented byte format, so the three things those
// rows demand (how many frames, what canvas, whether alpha is present) can be read
// directly, with no decoder and no dependency.
//
// It does NOT read pixels: a WebP whose frames were all blank would pass. Content
// for the libav-decodable formats is asserted elsewhere; for WebP the honest
// statement is "shape and alpha flag verified, pixels not".
struct WebpInfo {
    bool valid = false;
    bool animated = false;
    bool hasAlpha = false;
    int frames = 0;
    int width = 0;
    int height = 0;
};

static WebpInfo readWebpInfo(const QString& aPath) {
    WebpInfo info;
    QFile f(aPath);
    if (!f.open(QIODevice::ReadOnly)) return info;
    const QByteArray d = f.readAll();
    if (d.size() < 16) return info;
    if (d.mid(0, 4) != "RIFF" || d.mid(8, 4) != "WEBP") return info;

    auto u24 = [](const char* p) {
        return (int)((quint8)p[0] | ((quint8)p[1] << 8) | ((quint8)p[2] << 16));
    };
    int pos = 12;
    while (pos + 8 <= d.size()) {
        const QByteArray fourcc = d.mid(pos, 4);
        const quint32 size = (quint8)d[pos + 4] | ((quint8)d[pos + 5] << 8)
                             | ((quint8)d[pos + 6] << 16) | ((quint32)(quint8)d[pos + 7] << 24);
        const int payload = pos + 8;
        if (payload + int(size) > d.size()) break; // truncated: stop, do not guess

        if (fourcc == "VP8X" && size >= 10) {
            const quint8 flags = (quint8)d[payload];
            info.hasAlpha = (flags & 0x10) != 0;
            info.animated = (flags & 0x02) != 0;
            info.width = u24(d.constData() + payload + 4) + 1;
            info.height = u24(d.constData() + payload + 7) + 1;
            info.valid = true;
        } else if (fourcc == "ANMF") {
            ++info.frames;
        }
        pos = payload + int(size) + (int(size) & 1); // chunks are padded to even
    }
    return info;
}

// Pixel content for a format libav cannot decode.
//
// Animated WebP is the case: FFmpeg's demuxer answers "image data not found", so
// the frames could not be read back at all and the WebP rows were verified by
// container structure only. ImageMagick reads it (libwebp 1.6.0 here), and the
// harness already shells out to ffprobe, so this is the same kind of dependency —
// one more external tool that decodes what the linked libraries cannot.
//
// -coalesce is required, not optional: WebP animation frames are stored as
// sub-rectangles and deltas, so a raw decode yields partial frames (measured: 10
// frames of 128x13 instead of 128x96) and any ink measured on them would be
// meaningless.
struct MagickInfo {
    bool available = false;
    bool ok = false;
    int frames = 0;
    int width = 0;
    int height = 0;
    double inkPercent = -1.0;
    QString error;
};

static bool magickAvailable() {
    static const bool cached = [] {
        FILE* p = popen("magick -version >/dev/null 2>&1", "r");
        if (!p) return false;
        return pclose(p) == 0;
    }();
    return cached;
}

static MagickInfo magickRead(const QString& aPath) {
    MagickInfo info;
    info.available = magickAvailable();
    if (!info.available) {
        info.error = QStringLiteral("ImageMagick (magick) is not installed");
        return info;
    }

    // 1. the frame count and canvas, from the decoder rather than from the plan.
    {
        // The format string is repeated once PER FRAME with nothing between the
        // repetitions, so a separator is required: reading the first line of
        // "%n %w %h" gave "3 128 963 128 96..." and parsed the width as 963.
        // %W/%H are the canvas, which is the same for every frame.
        const QString cmd = QStringLiteral(
            "magick identify -format \"%n %W %H|\" \"%1\" 2>/dev/null").arg(aPath);
        FILE* p = popen(cmd.toUtf8().constData(), "r");
        if (!p) { info.error = QStringLiteral("could not run identify"); return info; }
        char buf[512] = {0};
        const bool got = fgets(buf, sizeof(buf), p) != nullptr;
        pclose(p);
        if (!got) { info.error = QStringLiteral("identify produced nothing"); return info; }
        const QString first = QString::fromUtf8(buf).section(QLatin1Char('|'), 0, 0).trimmed();
        const QStringList parts = first.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() < 3) {
            info.error = QStringLiteral("unexpected identify output: '%1'").arg(first);
            return info;
        }
        info.frames = parts.at(0).toInt();
        info.width = parts.at(1).toInt();
        info.height = parts.at(2).toInt();
    }

    // 2. the pixels, coalesced to full canvas frames.
    const QString raw = aeTestTempPath(QStringLiteral("ae_magick_raw"));
    {
        const QString cmd = QStringLiteral("magick \"%1\" -coalesce -depth 8 RGBA:\"%2\" "
                                           "2>/dev/null").arg(aPath, raw);
        FILE* p = popen(cmd.toUtf8().constData(), "r");
        if (!p) { info.error = QStringLiteral("could not run magick"); return info; }
        pclose(p);
    }
    QFile f(raw);
    if (!f.open(QIODevice::ReadOnly)) {
        info.error = QStringLiteral("magick wrote no pixels");
        QFile::remove(raw);
        return info;
    }
    const QByteArray d = f.readAll();
    f.close();
    QFile::remove(raw);

    const qint64 pixels = d.size() / 4;
    if (pixels <= 0) { info.error = QStringLiteral("magick produced an empty image"); return info; }
    qint64 ink = 0;
    for (qint64 i = 0; i + 3 < d.size(); i += 4) {
        const quint8 r = (quint8)d[i], g = (quint8)d[i + 1], b = (quint8)d[i + 2], a = (quint8)d[i + 3];
        if (r > 16 || g > 16 || b > 16 || a > 16) ++ink;
    }
    info.inkPercent = 100.0 * double(ink) / double(pixels);
    info.ok = true;
    return info;
}

static Probe probeFile(const QString& path, const char* aDecoderName = nullptr) {
    Probe p;
    AVFormatContext* fmt = nullptr;
    if (::avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) return p;
    ::avformat_find_stream_info(fmt, nullptr);
    const int vs = ::av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vs < 0) { ::avformat_close_input(&fmt); return p; }

    p.opened = true;
    p.codecTag = fmt->streams[vs]->codecpar->codec_tag;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) ++p.audioStreams;
    }

    const AVCodec* dec = aDecoderName
                             ? ::avcodec_find_decoder_by_name(aDecoderName)
                             : ::avcodec_find_decoder(fmt->streams[vs]->codecpar->codec_id);
    if (!dec) { ::avformat_close_input(&fmt); return p; }

    AVCodecContext* ctx = ::avcodec_alloc_context3(dec);
    ::avcodec_parameters_to_context(ctx, fmt->streams[vs]->codecpar);
    if (::avcodec_open2(ctx, dec, nullptr) < 0) {
        ::avcodec_free_context(&ctx);
        ::avformat_close_input(&fmt);
        return p;
    }
    p.width = ctx->width;
    p.height = ctx->height;

    // The sws context is built from the FIRST DECODED FRAME's format, not from
    // ctx->pix_fmt: a decoder's negotiated context format can differ from what
    // its frames actually carry (this is exactly how VP9 alpha hides — the
    // decoder emits yuva420p frames while the context still advertises a
    // non-alpha format, and scaling from the context format silently drops the
    // alpha plane).
    SwsContext* sws = nullptr;
    int swsFmt = -1;
    AVFrame* frame = ::av_frame_alloc();
    AVPacket* pkt = ::av_packet_alloc();

    auto pull = [&](AVFrame* f) {
        if (!sws || swsFmt != f->format) {
            if (sws) ::sws_freeContext(sws);
            swsFmt = f->format;
            sws = ::sws_getContext(ctx->width, ctx->height, (AVPixelFormat)f->format, ctx->width,
                                   ctx->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr,
                                   nullptr);
            if (!sws) return;
        }
        // scaleToRgba gives libswscale a padded destination: scaling into a
        // QImage sized exactly to the frame corrupts the heap (decode_util.h).
        const QImage img = scaleToRgba(sws, f, ctx->width, ctx->height);
        if (img.isNull()) return;
        ++p.frames;
        p.alphaDecoded = true;
        if (p.decodedPixFmt.isEmpty()) {
            p.decodedPixFmt = QString::fromUtf8(::av_get_pix_fmt_name((AVPixelFormat)f->format));
        }
        for (int y = 0; y < img.height(); ++y) {
            const uchar* line = img.constScanLine(y);
            for (int x = 0; x < img.width(); ++x) {
                const int a = line[4 * x + 3];
                if (a < p.minAlpha) p.minAlpha = a;
                if (a > p.maxAlpha) p.maxAlpha = a;
            }
        }
    };

    while (::av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vs) {
            ::avcodec_send_packet(ctx, pkt);
            while (::avcodec_receive_frame(ctx, frame) >= 0) pull(frame);
        }
        ::av_packet_unref(pkt);
    }
    ::avcodec_send_packet(ctx, nullptr);
    while (::avcodec_receive_frame(ctx, frame) >= 0) pull(frame);

    ::av_frame_free(&frame);
    ::av_packet_free(&pkt);
    if (sws) ::sws_freeContext(sws);
    ::avcodec_free_context(&ctx);
    ::avformat_close_input(&fmt);
    return p;
}

static int decodeInfo(const QString& path, int* outW, int* outH) {
    const Probe p = probeFile(path);
    *outW = p.width;
    *outH = p.height;
    return p.frames;
}

// ---------------------------------------------------------------------------

// Some destinations can only be produced on one OS (HEVC+alpha needs Apple's
// VideoToolbox). Those rows are declared, so validation can skip them instead
// of reporting them as broken.
static bool platformMatches(const QString& aPlatform) {
    if (aPlatform.isEmpty() || aPlatform == QLatin1String("any")) return true;
#ifdef Q_OS_MACOS
    if (aPlatform == QLatin1String("apple")) return true;
#endif
#ifdef Q_OS_WIN
    if (aPlatform == QLatin1String("windows")) return true;
#endif
#ifdef Q_OS_LINUX
    if (aPlatform == QLatin1String("linux")) return true;
#endif
    return false;
}

TEST_SECTION("load + validate every row", run_load_and_validate) {

    QString err;
    const QList<ctrl::ExportTarget> all =
        ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH), &err);
    check(err.isEmpty(), QStringLiteral("table parses (%1)").arg(err.isEmpty() ? QStringLiteral("ok") : err));
    check(all.size() >= 12, QStringLiteral("table has a meaningful row count (%1)").arg(all.size()));
    if (all.isEmpty()) return;

    // ids unique
    QSet<QString> seen;
    bool unique = true;
    for (const auto& t : all) {
        if (seen.contains(t.id)) unique = false;
        seen.insert(t.id);
    }
    check(unique, "destination ids are unique");

    // every row: container present; the accepted codec list is coherent; pinned
    // encoders exist and fit their muxer (I44: a LIST plus a declared default,
    // not the old fixed|ladder mode)
    int dictatedRows = 0, multiRows = 0, policyRows = 0;
    for (const auto& t : all) {
        if (t.container.isEmpty()) {
            check(false, QStringLiteral("%1: missing container").arg(t.id));
            continue;
        }
        if (!platformMatches(t.platform)) {
            QTextStream(stdout) << "      note: " << t.id << " is platform-specific ("
                                << t.platform << ") — skipped on this OS\n";
            continue;
        }
        if (t.artifact == ctrl::ArtifactKind::Video) {
            // I44: the schema is a list + a default, and the default is one of
            // the list (or empty = the class policy decides, constrained to it).
            check(!t.codecs.isEmpty(),
                  QStringLiteral("%1: accepts at least one codec").arg(t.id));
            check(t.defaultCodec.isEmpty() || t.codecs.contains(t.defaultCodec),
                  QStringLiteral("%1: the declared default '%2' is one of the accepted (%3)")
                      .arg(t.id, t.defaultCodec, t.codecs.join(QLatin1Char(' '))));
            if (t.codecs.size() == 1) ++dictatedRows;
            else ++multiRows;
            if (t.defaultCodec.isEmpty() && t.codecs.size() > 1) ++policyRows;
            // a pin's format must be one the row accepts
            if (!t.codec.isEmpty()) {
                check(t.codecs.contains(ctrl::formatKeyForEncoder(t.codec)),
                      QStringLiteral("%1: pin '%2' produces an accepted codec (%3)")
                          .arg(t.id, t.codec, t.codecs.join(QLatin1Char(' '))));
            }
        } else {
            // an image artifact has no codec to pick
            check(t.codecs.isEmpty(),
                  QStringLiteral("%1: an image row declares no codecs").arg(t.id));
        }
        const bool isGif = t.container.compare(QLatin1String("gif"), Qt::CaseInsensitive) == 0;
        if (isGif) continue; // media::GifEncoder owns GIF, not VideoEncoder
        // An image artifact has no codec to resolve: `container` names the image
        // format and Qt's writers produce the file. Its own section checks that the
        // format is writable, and the conformance loop writes and reads one back.
        if (t.artifact != ctrl::ArtifactKind::Video) continue;

        if (!t.codec.isEmpty()) {
            const QString resolved = media::ffmpeg::resolveEncoderName(t.codec);
            check(!resolved.isEmpty(),
                  QStringLiteral("%1: codec '%2' exists in this build").arg(t.id, t.codec));
            if (resolved.isEmpty()) continue;

            check(media::ffmpeg::muxerSupportsCodec(t.container, resolved),
                  QStringLiteral("%1: muxer '%2' can carry '%3'").arg(t.id, t.container, resolved));
        }
    }
    check(dictatedRows >= 8,
          QStringLiteral("dictated-codec rows present (%1)").arg(dictatedRows));
    check(multiRows >= 3, QStringLiteral("multi-acceptance rows present (%1)").arg(multiRows));
    check(policyRows >= 3, QStringLiteral("policy-default rows present (%1)").arg(policyRows));

    // provenance: every row is dated, and unverified rows are flagged as such
    int dated = 0, unverified = 0;
    for (const auto& t : all) {
        if (!t.checked.isEmpty()) ++dated;
        if (!t.verified) {
            ++unverified;
            QTextStream(stdout) << "      note: " << t.id << " is UNVERIFIED (checked " << t.checked << ")\n";
        }
    }
    check(dated == all.size(), QStringLiteral("every row carries a checked date (%1/%2)").arg(dated).arg(all.size()));
    // The guard used to be "at most ONE unverified row", written when the table was
    // fourteen destinations and an unverified fact was an oversight. It is now a
    // deliberate category: Tencent, Meta, Reddit and Bilibili simply do not publish
    // limits for video sent in a conversation, and for those the honest row states
    // that instead of inventing a number. So the guard that still means something is
    // not a COUNT but a REQUIREMENT: an unverified row must say so where the user can
    // read it, or "unverified" becomes a silent footnote again.
    {
        QStringList silent;
        for (const auto& t : all) {
            if (t.verified)
                continue;
            const bool saysSo = t.note.contains(QLatin1String("UNVERIFIED"), Qt::CaseInsensitive);
            if (!saysSo)
                silent << t.id;
        }
        check(silent.isEmpty(),
              QStringLiteral("every unverified row says so in its note (%1 unverified, %2 silent)%3")
                  .arg(unverified)
                  .arg(silent.size())
                  .arg(silent.isEmpty() ? QString() : QStringLiteral(": ") + silent.join(QLatin1String(", "))));
    }

    // The app loads this table from the embedded resource, not from disk. Prove
    // that path works — otherwise the default is only ever exercised in a full
    // app run, which is exactly where a missing qrc entry would hide.
    const QList<ctrl::ExportTarget> fromResource = ctrl::ExportTargets::load();
    check(fromResource.size() == all.size(),
          QStringLiteral("table loads from the app resource %1 (%2 rows)")
              .arg(ctrl::ExportTargets::defaultPath())
              .arg(fromResource.size()));
}


TEST_SECTION("size rules", run_size_rules) {
    // THE TABLE IS DATA AND THE EXTRACTOR CANNOT READ IT. Every user-visible string in
    // it is repeated as a QT_TRANSLATE_NOOP entry in src/ctrl/ExportTargetStrings.h
    // (generated), which is what makes the labels and the notes translatable at all.
    // A generated list drifts silently, so this asserts it against the data file in
    // BOTH directions: every label, title and note has an entry, and no entry is left
    // over from a row that no longer exists.
    {
        const QList<ctrl::ExportTarget> stringRows =
            ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));
        const QList<ctrl::ExportGroup> stringGroups =
            ctrl::ExportTargets::loadGroups(QStringLiteral(AE_EXPORT_TARGETS_PATH));
        const QStringList entries = ctrl::exportTargetStringList();
        check(entries.size() > 100,
              QStringLiteral("the generated string list carries the table (%1 entries)").arg(entries.size()));

        QStringList wanted;
        for (const ctrl::ExportGroup& g : stringGroups) wanted << g.title;
        for (const ctrl::ExportTarget& t : stringRows) {
            wanted << t.label << t.note; // notes are one line by the time they get here
        }
        wanted.removeAll(QString());
        wanted.removeDuplicates();

        QStringList missing, stale;
        for (const QString& w : wanted) {
            if (!entries.contains(w)) missing << w;
        }
        for (const QString& e : entries) {
            if (!wanted.contains(e)) stale << e;
        }
        // Name the first offender of each kind: a bare count sends the reader back to
        // the generator to find out what changed.
        if (!missing.isEmpty()) {
            check(false, QStringLiteral("the string list is missing a table string: %1")
                             .arg(missing.first().left(70)));
        }
        if (!stale.isEmpty()) {
            check(false, QStringLiteral("the string list has a stale entry: %1")
                             .arg(stale.first().left(70)));
        }
        check(missing.isEmpty() && stale.isEmpty(),
              QStringLiteral("the extracted strings and the table agree (%1 strings, %2 missing, "
                             "%3 stale)")
                  .arg(entries.size()).arg(missing.size()).arg(stale.size()));
    }

    // AN EXACT SIZE NEVER STRETCHES THE ART. The remainder is padded (or cropped, for
    // a row whose platform forbids margins), and which of the two is a per-row data
    // fact - so this walks every exact row in the table and asserts the plan carries
    // one of them. A plan with fit == None and an exact canvas is the "distorted to
    // fit" case a sticker destination must never produce.
    {
        const QList<ctrl::ExportTarget> rows =
            ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));
        int exactRows = 0, padded = 0, cropped = 0;
        for (const ctrl::ExportTarget& t : rows) {
            if (t.size.mode != ctrl::SizeMode::Exact) continue;
            ++exactRows;
            ctrl::ExportRequest req;
            // Deliberately the wrong shape: an exact rule is where a mismatched art
            // has to be reconciled rather than stretched.
            req.sourceSize = QSize(1000, 400);
            req.selection = ctrl::FrameSelection::fromCount(1);
            const ctrl::ExportPlan plan = ctrl::planExport(t, req, ctrl::QualityBucket::Balanced);
            if (!plan.ok()) {
                check(false, QStringLiteral("%1: exact-size plan builds (%2)").arg(t.id, plan.error));
                continue;
            }
            check(plan.config.fit != media::FitMode::None,
                  QStringLiteral("%1: an exact canvas pads or crops, never stretches").arg(t.id));
            if (plan.config.fit == media::FitMode::Crop) ++cropped;
            else ++padded;
            if (plan.size != QSize(t.size.w, t.size.h)) {
                check(false, QStringLiteral("%1: exact rule produced %2x%3, not %4x%5")
                                 .arg(t.id).arg(plan.size.width()).arg(plan.size.height())
                                 .arg(t.size.w).arg(t.size.h));
            }
        }
        check(exactRows >= 6, QStringLiteral("every exact row in the table was walked (%1)").arg(exactRows));
        check(padded + cropped == exactRows,
              QStringLiteral("and each one resolved to a mode (%1 padded, %2 cropped)")
                  .arg(padded).arg(cropped));
    }

    // A destination can declare that the frame rate never reaches it, and the form
    // then does not ask for one. pixiv ugoira is the case: numbered stills whose
    // timing is authored on the upload page.
    {
        const QList<ctrl::ExportTarget> rows =
            ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));
        const ctrl::ExportTarget ugoira =
            ctrl::ExportTargets::find(rows, QStringLiteral("post.pixiv.ugoira"));
        check(ugoira.id == QLatin1String("post.pixiv.ugoira"), "found post.pixiv.ugoira");
        check(ugoira.fpsIgnored, "ugoira declares that the frame rate is not its business");
        check(ugoira.caps.maxFrames == 150,
              QStringLiteral("and its frame COUNT still applies (%1)").arg(ugoira.caps.maxFrames));
        // The flag is about the frame rate, not about the artifact: the row is still a
        // sequence, which is what makes it take a frame RANGE.
        check(ugoira.artifact == ctrl::ArtifactKind::Sequence, "and it is still a sequence");
        for (const ctrl::ExportTarget& t : rows) {
            if (t.fpsIgnored && t.artifact == ctrl::ArtifactKind::Video) {
                check(false, QStringLiteral("%1: a video row claims to ignore the frame rate").arg(t.id));
            }
        }
    }

    // A Free rule means the destination imposes no size. That is NOT the same as
    // deciding one: the export dialog's own Width/Height stay in force, and the
    // plan must echo them. Without this a 1920x1080 upload from a 1100x790
    // project was impossible, and — worse — a size forced by the PREVIOUS
    // destination (Twitch's 112px) could stand in as if it were the user's.
    {
        const ctrl::ExportTarget freeRow =
            ctrl::ExportTargets::find(ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH)),
                                     QStringLiteral("youtube.video"));
        check(freeRow.size.mode == ctrl::SizeMode::Free, "youtube.video imposes no size");

        ctrl::ExportRequest req;
        req.sourceSize = QSize(1100, 790);
        req.selection = ctrl::FrameSelection::fromCount(1);
        const ctrl::ExportPlan defaults = ctrl::planExport(freeRow, req, ctrl::QualityBucket::Balanced);
        check(defaults.ok() && defaults.size == QSize(1100, 790),
              QStringLiteral("no requested size -> the art's own (%1x%2)")
                  .arg(defaults.size.width()).arg(defaults.size.height()));

        req.requestedSize = QSize(1920, 1080);
        const ctrl::ExportPlan chosen = ctrl::planExport(freeRow, req, ctrl::QualityBucket::Balanced);
        check(chosen.ok() && chosen.size == QSize(1920, 1080),
              QStringLiteral("requested size is used on a Free rule (%1x%2)")
                  .arg(chosen.size.width()).arg(chosen.size.height()));

        // Odd dimensions are not a user error: YUV needs even, so floor.
        req.requestedSize = QSize(1921, 1081);
        const ctrl::ExportPlan odd = ctrl::planExport(freeRow, req, ctrl::QualityBucket::Balanced);
        check(odd.size == QSize(1920, 1080),
              QStringLiteral("odd requested size floors to even (%1x%2)")
                  .arg(odd.size.width()).arg(odd.size.height()));

        // A MAX rule is a CEILING, not a decision. The user's own resolution is the
        // other ceiling and the art fits inside the smaller of the two, so "1280x720
        // maximum, but I want 640x360" is expressible - and the dialog can leave the
        // Width/Height boxes live instead of freezing them for every max-mode row
        // (Discord, WhatsApp, Signal, Instagram, TikTok).
        {
            const ctrl::ExportTarget capped =
                ctrl::ExportTargets::find(ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH)),
                                          QStringLiteral("chat.discord.file"));
            check(capped.size.mode == ctrl::SizeMode::Max,
                  "the Discord chat row bounds the size rather than pinning it");

            ctrl::ExportRequest maxReq;
            maxReq.sourceSize = QSize(1920, 1080);
            maxReq.selection = ctrl::FrameSelection::fromCount(1);

            const ctrl::ExportPlan unbounded = ctrl::planExport(capped, maxReq, ctrl::QualityBucket::Balanced);
            check(unbounded.ok() && unbounded.size == QSize(1280, 720),
                  QStringLiteral("with no request the art fits the destination's ceiling (%1x%2)")
                      .arg(unbounded.size.width()).arg(unbounded.size.height()));

            maxReq.requestedSize = QSize(640, 360);
            const ctrl::ExportPlan smaller = ctrl::planExport(capped, maxReq, ctrl::QualityBucket::Balanced);
            check(smaller.ok() && smaller.size == QSize(640, 360),
                  QStringLiteral("a smaller requested size is honoured inside it (%1x%2)")
                      .arg(smaller.size.width()).arg(smaller.size.height()));

            maxReq.requestedSize = QSize(3840, 2160);
            const ctrl::ExportPlan tooBig = ctrl::planExport(capped, maxReq, ctrl::QualityBucket::Balanced);
            check(tooBig.ok() && tooBig.size == QSize(1280, 720),
                  QStringLiteral("and a larger one is still capped by the destination (%1x%2)")
                      .arg(tooBig.size.width()).arg(tooBig.size.height()));

            // The ceiling also has to hold for a requested size that is bigger on one
            // axis and smaller on the other: the fit is inside the SMALLER box, since
            // that is what both limits say.
            maxReq.requestedSize = QSize(3840, 360);
            const ctrl::ExportPlan mixed = ctrl::planExport(capped, maxReq, ctrl::QualityBucket::Balanced);
            check(mixed.ok() && mixed.size.width() <= 1280 && mixed.size.height() <= 360,
                  QStringLiteral("both ceilings hold (%1x%2)")
                      .arg(mixed.size.width()).arg(mixed.size.height()));
        }

        // The new animated-image row has to be a WebP that this build can actually
        // produce, which the WebP section below reads back out of the container.
        {
            const ctrl::ExportTarget anim =
                ctrl::ExportTargets::find(ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH)),
                                          QStringLiteral("discord.animation"));
            check(anim.id == QLatin1String("discord.animation"), "found discord.animation");
            check(anim.container == QLatin1String("webp") && anim.codec == QLatin1String("libwebp"),
                  "Discord's animated image is WebP (the format Discord's own pipeline produces)");
            check(anim.caps.maxBytes == 10485760,
                  QStringLiteral("capped at the inline display limit (%1 bytes)").arg(anim.caps.maxBytes));
        }

        // The ASPECT a fit rule preserves must stay the ART's, so a size typed
        // for a free destination cannot leak into an exact one's padding.
        req.requestedSize = QSize(1920, 1080);
        req.sourceSize = QSize(1000, 800);
        const ctrl::ExportTarget discordRow =
            ctrl::ExportTargets::find(ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH)),
                                     QStringLiteral("discord.sticker"));
        const ctrl::ExportPlan exact = ctrl::planExport(discordRow, req, ctrl::QualityBucket::Balanced);
        check(exact.ok() && exact.size == QSize(320, 320),
              "an exact rule ignores requestedSize (the destination decides)");
        check(exact.renderSize == QSize(320, 256),
              QStringLiteral("exact renderSize uses the ART's aspect, not the request's (%1x%2)")
                  .arg(exact.renderSize.width()).arg(exact.renderSize.height()));
    }

    const ctrl::ExportTarget telegram =
        ctrl::ExportTargets::find(ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH)),
                                 QStringLiteral("telegram.sticker"));
    check(telegram.id == QLatin1String("telegram.sticker"), "found telegram.sticker");

    // Telegram: one side exactly 512, other <= 512.
    const QSize a = ctrl::applySizeRule(telegram.size, QSize(1000, 800));
    check(a.width() == 512 && a.height() <= 512 && a.height() > 0,
          QStringLiteral("telegram 1000x800 -> %1x%2 (one side exactly 512)").arg(a.width()).arg(a.height()));
    const QSize b = ctrl::applySizeRule(telegram.size, QSize(1000, 1000));
    check(b == QSize(512, 512), QStringLiteral("telegram 1000x1000 -> 512x512 (got %1x%2)").arg(b.width()).arg(b.height()));

    // LINE: fit inside 320x270, long side >= 270.
    const ctrl::ExportTarget line =
        ctrl::ExportTargets::find(ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH)),
                                 QStringLiteral("line.sticker"));
    const QSize c = ctrl::applySizeRule(line.size, QSize(1000, 800));
    check(c.width() <= 320 && c.height() <= 270 && std::max(c.width(), c.height()) >= 270,
          QStringLiteral("LINE 1000x800 -> %1x%2 (within 320x270, long side >= 270)").arg(c.width()).arg(c.height()));

    // A MAXIMUM must not enlarge. Every max-mode row (the chat and post video
    // destinations) used to scale a small project UP to the box, so a 320x240
    // animation exported to a 1080x1920 destination came out 1080x810: interpolated
    // pixels, more bytes and more encode time, for nothing the platform asked for.
    {
        const ctrl::ExportTarget tiktok =
            ctrl::ExportTargets::find(ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH)),
                                     QStringLiteral("post.tiktok.video"));
        check(tiktok.size.mode == ctrl::SizeMode::Max, "the TikTok row is a max rule");
        const QSize small = ctrl::applySizeRule(tiktok.size, QSize(320, 240));
        check(small == QSize(320, 240),
              QStringLiteral("a max rule leaves a small project alone (320x240 -> %1x%2)")
                  .arg(small.width()).arg(small.height()));
        const QSize big = ctrl::applySizeRule(tiktok.size, QSize(4000, 3000));
        check(big.width() <= 1080 && big.height() <= 1920 && big != QSize(4000, 3000),
              QStringLiteral("a max rule still shrinks what is too big (4000x3000 -> %1x%2)")
                  .arg(big.width()).arg(big.height()));
    }

    // Discord: exactly 320x320.
    const ctrl::ExportTarget discord =
        ctrl::ExportTargets::find(ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH)),
                                 QStringLiteral("discord.sticker"));
    const QSize d = ctrl::applySizeRule(discord.size, QSize(1000, 800));
    check(d == QSize(320, 320), QStringLiteral("discord 1000x800 -> exactly 320x320 (got %1x%2)").arg(d.width()).arg(d.height()));

    // Exact-size rows must also say what to RENDER at, so non-square art is
    // letterboxed with transparency rather than stretched.
    ctrl::ExportRequest exactReq;
    exactReq.outputPath = aeTestTempPath("ae_exact.apng");
    exactReq.sourceSize = QSize(1000, 800);
    exactReq.selection = ctrl::FrameSelection::fromCount(1);
    const ctrl::ExportPlan exactPlan =
        ctrl::planExport(discord, exactReq, ctrl::QualityBucket::Balanced);
    check(exactPlan.ok(), QStringLiteral("exact-size plan builds (%1)").arg(exactPlan.error));
    check(exactPlan.renderSize == QSize(320, 256),
          QStringLiteral("renderSize fits inside the exact box (got %1x%2)")
              .arg(exactPlan.renderSize.width())
              .arg(exactPlan.renderSize.height()));
    check(exactPlan.config.fit == media::FitMode::Pad,
          "exact-size rows default to Pad (fit + transparent margin, never stretch)");

    // ...but pad is a CHOICE, not a law: platforms disagree about margins
    // (Signal asks for 16px, WhatsApp an 8px stroke, LINE forbids them), so a
    // destination may declare crop instead.
    ctrl::ExportTarget croppable = discord;
    croppable.size.fit = ctrl::FitMode::Crop;
    const ctrl::ExportPlan cropPlan =
        ctrl::planExport(croppable, exactReq, ctrl::QualityBucket::Balanced);
    check(cropPlan.config.fit == media::FitMode::Crop,
          "a destination declaring fit=crop gets Crop");

    // Signal: EXACTLY 512x512 (stickers resize to 512). It used to be declared as a
    // max rule, which only produced 512x512 while max-mode rules enlarged small art
    // — so a 200x200 project came out 512x512 by way of an upscale, and the moment
    // max stopped enlarging (a maximum should cap, not grow) this row would have
    // exported 200x200 to a platform that requires 512. The requirement was always
    // the size, so the rule now says so.
    const ctrl::ExportTarget signal =
        ctrl::ExportTargets::find(ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH)),
                                 QStringLiteral("signal.sticker"));
    const QSize e = ctrl::applySizeRule(signal.size, QSize(200, 200));
    check(e == QSize(512, 512), QStringLiteral("signal 200x200 -> 512x512 (got %1x%2)").arg(e.width()).arg(e.height()));

    // Even dimensions for YUV.
    const QSize f = ctrl::applySizeRule(telegram.size, QSize(1001, 801));
    check((f.width() % 2) == 0 && (f.height() % 2) == 0,
          QStringLiteral("odd source still yields even output (%1x%2)").arg(f.width()).arg(f.height()));
}

TEST_SECTION("caps (deterministic pre-encode enforcement)", run_caps) {

    const ctrl::ExportTarget telegram =
        ctrl::ExportTargets::find(ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH)),
                                 QStringLiteral("telegram.sticker"));

    int fps = 60, frames = 300; // 5 seconds at 60fps
    QStringList notes;
    ctrl::applyCaps(telegram.caps, &fps, &frames, &notes);
    check(fps == 30, QStringLiteral("fps clamped to 30 (got %1)").arg(fps));
    check(frames == 90, QStringLiteral("duration clamped to 3s -> 90 frames (got %1)").arg(frames));

    // LINE's minimum frame count cannot be invented — it must WARN, not silently pass.
    const ctrl::ExportTarget line =
        ctrl::ExportTargets::find(ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH)),
                                 QStringLiteral("line.sticker"));
    int lfps = 24, lframes = 3;
    QStringList lnotes;
    ctrl::applyCaps(line.caps, &lfps, &lframes, &lnotes);
    const bool warned = lnotes.filter(QStringLiteral("WARNING")).size() == 1;
    check(warned, "LINE min-frames (5) under-run produces a warning, not a silent pass");

    // A MINIMUM duration (Snapchat Spotlight: a Spot must be 5 seconds or longer)
    // is the mirror of maxDurationSec and cannot be met by exporting less, so it is
    // stated rather than applied. Before this existed the dialog said nothing at
    // all, and the file was refused at upload with no warning anywhere.
    const ctrl::ExportTarget spot =
        ctrl::ExportTargets::find(ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH)),
                                 QStringLiteral("post.snapchat.spotlight"));
    check(spot.caps.minDurationSec == 5.0,
          QStringLiteral("the Spotlight row carries its 5 s floor (got %1)").arg(spot.caps.minDurationSec));
    {
        int sfps = 30, sframes = 60; // 2 seconds, under the floor
        QStringList snotes;
        ctrl::applyCaps(spot.caps, &sfps, &sframes, &snotes);
        const QString span = snotes.join(QLatin1Char(' '));
        check(span.contains(QLatin1String("at least 5")),
              QStringLiteral("a 2 s export to Spotlight is warned about (%1)").arg(span));
        check(sframes == 60 && sfps == 30,
              "a minimum does not silently change what is exported");
    }
    {
        int sfps = 30, sframes = 300; // 10 seconds, over the floor
        QStringList snotes;
        ctrl::applyCaps(spot.caps, &sfps, &sframes, &snotes);
        check(snotes.join(QLatin1Char(' ')).contains(QLatin1String("at least 5")) == false,
              "a 10 s export to Spotlight is not warned about");
    }

    // The caps must be applied exactly ONCE by planExport. It used to reset
    // fps/frames back to the requested values and re-apply them at the end,
    // which clamped to the same numbers but re-derived and re-appended every
    // cap note, so the user saw each warning twice.
    ctrl::ExportRequest creq;
    creq.outputPath = aeTestTempPath("ae_caps_once.png");
    creq.sourceSize = QSize(320, 240);
    creq.fps = 30;
    creq.selection = ctrl::FrameSelection::fromCount(50); // LINE's cap is 20
    const ctrl::ExportPlan cplan = ctrl::planExport(line, creq, ctrl::QualityBucket::Balanced);
    check(cplan.ok(), QStringLiteral("the capped plan builds (%1)").arg(cplan.error));
    check(cplan.frames() == 20, QStringLiteral("LINE's cap lands on 20 frames (got %1)").arg(cplan.frames()));
    QSet<QString> seenNotes;
    bool duplicated = false;
    for (const QString& n : cplan.notes) {
        if (seenNotes.contains(n)) duplicated = true;
        seenNotes.insert(n);
    }
    check(!duplicated, QStringLiteral("no note is duplicated by the cap pass"));
}

TEST_SECTION("frame selection (several non-contiguous ranges)", run_frame_selection) {

    using ctrl::FrameRange;
    using ctrl::FrameSelection;

    const FrameSelection one = FrameSelection::fromRange(10, 19);
    check(one.total() == 10, QStringLiteral("fromRange(10,19) is 10 frames (got %1)").arg(one.total()));
    check(one.rangeCount() == 1, QStringLiteral("fromRange is one range"));
    check(FrameSelection::fromCount(5).total() == 5, QStringLiteral("fromCount(5) is 5 frames"));
    check(FrameSelection::fromCount(1).indices() == (QList<int>{0}),
          QStringLiteral("fromCount(1) is frame 0 alone"));
    check(FrameSelection::fromCount(0).isEmpty(), QStringLiteral("fromCount(0) is empty"));
    check(FrameSelection::fromRange(5, 4).isEmpty(),
          QStringLiteral("a backwards range contributes nothing"));
    // Bounds are inclusive, and negative frames are ordinary in this codebase
    // (util::Range supports them), so a negative selection must survive.
    check(FrameSelection::fromRange(-3, -1).indices() == (QList<int>{-3, -2, -1}),
          QStringLiteral("negative ranges are inclusive and preserved"));

    // THE reason this type exists: ranges are PACKED and the gaps are dropped.
    // The old model collapsed them to [first..last] and exported frames the user
    // had excluded.
    FrameSelection multi;
    multi.ranges.append(FrameRange{0, 10});
    multi.ranges.append(FrameRange{50, 60});
    check(multi.total() == 22,
          QStringLiteral("0-10 + 50-60 is 22 frames, not 61 (got %1)").arg(multi.total()));
    check(multi.rangeCount() == 2, QStringLiteral("two usable ranges"));
    const QList<int> idx = multi.indices();
    check(idx.size() == 22, QStringLiteral("indices() yields exactly what is written (got %1)").arg(idx.size()));
    check(idx.value(10) == 10 && idx.value(11) == 50 && idx.last() == 60,
          QStringLiteral("indices() jumps the gap: element 10 -> frame 10, element 11 -> frame 50"));
    check(!idx.contains(11) && !idx.contains(49),
          QStringLiteral("nothing from inside the gap is present"));

    // Order is honoured, not sorted, so the output matches the list on screen.
    FrameSelection reversed;
    reversed.ranges.append(FrameRange{50, 51});
    reversed.ranges.append(FrameRange{0, 1});
    check(reversed.indices() == (QList<int>{50, 51, 0, 1}),
          QStringLiteral("range order is honoured rather than sorted"));

    // Overlaps are not merged: total() must equal the frames actually written,
    // and repeating a frame is a real way to hold one.
    FrameSelection overlap;
    overlap.ranges.append(FrameRange{0, 10});
    overlap.ranges.append(FrameRange{5, 15});
    check(overlap.total() == 22,
          QStringLiteral("overlapping ranges count twice (got %1)").arg(overlap.total()));

    // firstN is the truncation cap enforcement uses, so it must be monotone:
    // a smaller N is always a prefix of a larger one.
    check(multi.firstN(0).isEmpty(), QStringLiteral("firstN(0) is empty"));
    check(multi.firstN(5).indices() == (QList<int>{0, 1, 2, 3, 4}),
          QStringLiteral("firstN(5) takes the first five"));
    check(multi.firstN(15).total() == 15, QStringLiteral("firstN(15) spans the gap and totals 15"));
    check(multi.firstN(15).indices().last() == 53,
          QStringLiteral("firstN crosses into the second range (11 + 4, last=%1)")
              .arg(multi.firstN(15).indices().last()));
    check(multi.firstN(15).rangeCount() == 2, QStringLiteral("firstN keeps the range structure"));
    check(multi.firstN(999).total() == multi.total(), QStringLiteral("firstN past the end is a no-op"));

    // describe() is what the user reads.
    check(one.describe() == QLatin1String("10 frames"),
          QStringLiteral("a single range describes as a count (got '%1')").arg(one.describe()));
    check(FrameSelection::fromCount(1).describe() == QLatin1String("1 frame"),
          QStringLiteral("singular is not pluralised"));
    check(multi.describe().contains(QLatin1String("2 separate ranges")),
          QStringLiteral("a multi-range selection says so (got '%1')").arg(multi.describe()));
    check(FrameSelection().describe().contains(QLatin1String("no frames")),
          QStringLiteral("an empty selection describes clearly"));

    // --- through the planner -------------------------------------------------
    const QList<ctrl::ExportTarget> all =
        ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));
    if (all.isEmpty()) return;
    const ctrl::ExportTarget generic =
        ctrl::ExportTargets::find(all, QStringLiteral("generic.file")); // no caps
    const ctrl::ExportTarget line =
        ctrl::ExportTargets::find(all, QStringLiteral("line.sticker")); // 5-20 frames

    ctrl::ExportRequest req;
    req.outputPath = aeTestTempPath("ae_selection.mp4");
    req.sourceSize = QSize(320, 240);
    req.fps = 30;
    req.selection = multi;

    const ctrl::ExportPlan plan = ctrl::planExport(generic, req, ctrl::QualityBucket::Balanced);
    check(plan.ok(), QStringLiteral("a multi-range plan builds (%1)").arg(plan.error));
    check(plan.frames() == 22, QStringLiteral("plan.frames() is the selection total (got %1)").arg(plan.frames()));
    check(plan.selection.indices() == multi.indices(),
          QStringLiteral("the plan carries the selection through unchanged"));
    bool gapNote = false;
    for (const QString& n : plan.notes) {
        if (n.contains(QLatin1String("back to back"))) gapNote = true;
    }
    check(gapNote, QStringLiteral("the plan explains that the gaps are dropped"));

    // A cap must truncate the SELECTION, not merely a counter — otherwise the
    // caller cannot tell which frames survived.
    ctrl::ExportRequest lreq = req;
    lreq.outputPath = aeTestTempPath("ae_selection.png");
    lreq.selection = FrameSelection::fromCount(50);
    const ctrl::ExportPlan lplan = ctrl::planExport(line, lreq, ctrl::QualityBucket::Balanced);
    check(lplan.ok(), QStringLiteral("the capped plan builds (%1)").arg(lplan.error));
    check(lplan.frames() == 20, QStringLiteral("LINE's 20-frame cap applies (got %1)").arg(lplan.frames()));
    check(lplan.selection.total() == 20,
          QStringLiteral("the selection itself is truncated, not just the count"));
    check(lplan.selection.indices().last() == 19,
          QStringLiteral("the cap keeps the FIRST N frames (last=%1)").arg(lplan.selection.indices().last()));

    // (The "cap notes appear exactly once" check lives in run_caps, where the
    // cap behaviour is tested; it is not repeated here.)

    // An empty selection is refused, rather than silently planning 0 frames.
    ctrl::ExportRequest ereq = req;
    ereq.selection = FrameSelection();
    check(!ctrl::planExport(generic, ereq, ctrl::QualityBucket::Balanced).ok(),
          QStringLiteral("an empty selection is an error, not a zero-frame export"));

    // exportWithFit must refuse a hand-off that does not match the plan instead
    // of encoding a file the plan never described.
    QList<QImage> wrong;
    for (int i = 0; i < 5; ++i) wrong.append(makeFrame(320, 240, i));
    const ctrl::FitResult fr =
        ctrl::exportWithFit(generic, req, ctrl::QualityBucket::Balanced, wrong);
    // The source wording, not a translation: this test runs with no translator
    // installed. It asserts the label form ("frames: ...") that replaced the
    // "%1 frame"/"%1 frames" pair, because a noun with a number in front of it cannot
    // be rendered correctly for every count in every language.
    check(!fr.ok && fr.error.contains(QLatin1String("frames: the plan selected")),
          QStringLiteral("a frame-count mismatch is refused (%1)").arg(fr.error));
}

TEST_SECTION("availability probe: name lookup vs real one-frame encode", run_probe_sanity) {

    // The point of this section: encoderAvailable() only proves an encoder was
    // compiled in. Hardware encoders are listed on machines that cannot run
    // them, so the ladder MUST gate on encoderUsable(). Report every name where
    // the two disagree — that is the false-positive set we would otherwise
    // have selected.
    const char* hwNames[] = {"av1_nvenc", "hevc_nvenc", "h264_nvenc",
                             "av1_qsv",   "hevc_qsv",   "h264_qsv",
                             "av1_vaapi", "hevc_vaapi", "h264_vaapi",
                             "av1_amf",   "hevc_amf",   "h264_amf"};
    int disagree = 0;
    for (const char* n : hwNames) {
        const bool listed = media::ffmpeg::encoderAvailable(n);
        const bool usable = media::ffmpeg::encoderUsable(n);
        if (listed != usable) {
            ++disagree;
            QTextStream(stdout) << "      " << n << ": listed=" << (listed ? "yes" : "no")
                                << " usable=" << (usable ? "yes" : "no") << "\n";
        }
    }
    QTextStream(stdout) << "      hardware encoders where listed != usable: " << disagree << "\n";
    check(true, "hardware-encoder disagreement reported (informational)");

    // Software encoders we rely on must be genuinely usable.
    check(media::ffmpeg::encoderUsable("libx264"), "libx264 is usable");
    check(media::ffmpeg::encoderUsable("libx265"), "libx265 is usable");
}

TEST_SECTION("codec ladder (class x bucket)", run_ladder) {

    const ctrl::DestClass classes[] = {ctrl::DestClass::Reencode, ctrl::DestClass::Direct,
                                       ctrl::DestClass::Archive};
    const ctrl::QualityBucket buckets[] = {ctrl::QualityBucket::Smaller, ctrl::QualityBucket::Balanced,
                                           ctrl::QualityBucket::Best, ctrl::QualityBucket::Lossless};
    const char* classNames[] = {"reencode", "direct", "archive"};
    const char* bucketNames[] = {"smaller", "balanced", "best", "lossless"};

    for (int ci = 0; ci < 3; ++ci) {
        for (int bi = 0; bi < 4; ++bi) {
            QString why;
            const QString codec = ctrl::resolveLadderCodec(classes[ci], buckets[bi], &why);
            const bool ok = !codec.isEmpty() && media::ffmpeg::encoderUsable(codec.toUtf8().constData());
            check(ok, QStringLiteral("%1/%2 -> %3").arg(classNames[ci], bucketNames[bi],
                                                         ok ? codec : QStringLiteral("NONE")));
            if (ok) QTextStream(stdout) << "      why: " << why << "\n";
        }
    }

    // The one ordering claim the evidence actually supports: direct playback
    // must favour decode compatibility over efficiency at every bucket.
    bool allH264 = true;
    for (int bi = 0; bi < 4; ++bi) {
        const QString codec =
            ctrl::resolveLadderCodec(ctrl::DestClass::Direct, buckets[bi]);
        if (!codec.contains(QLatin1String("264"))) allH264 = false;
    }
    check(allH264, "direct playback picks H.264 at every bucket (compatibility first)");

    // ---- the ladder's policy, which is now policy and nothing else -----------
    //
    // Three claims, each a fact about the TABLE and about what an encoder is built for,
    // and none of them about this machine's speed:
    //
    //   * the FAMILY order (quality per bit: AV1, then H.265, then H.264) is the same at
    //     every bucket, and "lossless" leads where losslessness is the requirement;
    //   * inside a family, the time-sensitive buckets take a HARDWARE encoder when one
    //     runs, because that is what a hardware encoder is for;
    //   * the fidelity buckets keep the declared (software) order, because software
    //     exposes the rate control they are paying for.
    //
    // The old version measured eight black frames per candidate and asserted the pick
    // was the fastest of them. That is gone: it was slow (libaom measured 15.7 s for
    // those eight frames here against libsvtav1's 1.0 s), it compared encoders doing
    // different amounts of work (each library's DEFAULT effort, which sit at opposite
    // ends of their ranges), and it made the user's export depend on an unstated
    // patience threshold.
    {
        auto familyOf = [](const QString& aEncoder) -> QString {
            if (aEncoder.contains(QLatin1String("av1")) || aEncoder == QLatin1String("libaom-av1"))
                return QStringLiteral("av1");
            if (aEncoder.contains(QLatin1String("265")) || aEncoder.contains(QLatin1String("hevc")))
                return QStringLiteral("hevc");
            if (aEncoder.contains(QLatin1String("264"))) return QStringLiteral("h264");
            if (aEncoder == QLatin1String("ffv1")) return QStringLiteral("lossless");
            return QString();
        };
        auto isHardware = [](const QString& aEncoder) {
            return aEncoder.contains(QLatin1String("nvenc")) || aEncoder.contains(QLatin1String("qsv"))
                   || aEncoder.contains(QLatin1String("vaapi")) || aEncoder.contains(QLatin1String("amf"))
                   || aEncoder.contains(QLatin1String("videotoolbox"));
        };

        QString whyLossless;
        const QString losslessPick = ctrl::resolveLadderCodec(
            ctrl::DestClass::Reencode, ctrl::QualityBucket::Lossless, &whyLossless);
        check(losslessPick == QLatin1String("ffv1") || familyOf(losslessPick) == QLatin1String("lossless"),
              QStringLiteral("the lossless bucket leads with the lossless family (%1)").arg(losslessPick));

        const bool av1Runs = media::ffmpeg::encoderUsable("libsvtav1")
                             || media::ffmpeg::encoderUsable("libaom-av1");
        const QString bestPick = ctrl::resolveLadderCodec(ctrl::DestClass::Reencode,
                                                          ctrl::QualityBucket::Best);
        if (av1Runs) {
            check(familyOf(bestPick) == QLatin1String("av1"),
                  QStringLiteral("the Best bucket leads with the most efficient family (%1)").arg(bestPick));
        } else {
            QTextStream(stdout) << "      (no AV1 encoder in this build; family order untested here)\n";
        }

        // Every family's own order: hardware first for the time-sensitive buckets.
        const struct { const char* family; QStringList encoders; } families[] = {
            {"av1", {"libsvtav1", "libaom-av1", "av1_nvenc", "av1_qsv", "av1_vaapi", "av1_amf"}},
            {"hevc", {"libx265", "hevc_nvenc", "hevc_qsv", "hevc_vaapi", "hevc_amf"}},
            {"h264", {"libx264", "h264_nvenc", "h264_qsv", "h264_vaapi", "h264_amf"}},
        };
        for (const auto& fam : families) {
            QStringList running;
            for (const QString& c : fam.encoders) {
                if (media::ffmpeg::encoderUsable(c.toUtf8().constData())) running << c;
            }
            if (running.isEmpty()) continue;
            QStringList hardware;
            for (const QString& c : running) {
                if (isHardware(c)) hardware << c;
            }
            const QString expectedBalanced = hardware.isEmpty() ? running.first() : hardware.first();

            // Reencode at Balanced: the format is AV1 when it runs, hevc otherwise, so
            // this is asked per family by name only when that family is the one chosen.
            const QString balancedPick =
                ctrl::resolveLadderCodec(ctrl::DestClass::Reencode, ctrl::QualityBucket::Balanced);
            if (familyOf(balancedPick) == QLatin1String(fam.family)) {
                check(balancedPick == expectedBalanced,
                      QStringLiteral("%1 at Balanced takes %2 of the %3 that run (%4)")
                          .arg(fam.family, expectedBalanced)
                          .arg(running.size())
                          .arg(running.join(QStringLiteral(", "))));
            }
            const QString bestInFamily =
                ctrl::resolveLadderCodec(ctrl::DestClass::Reencode, ctrl::QualityBucket::Best);
            if (familyOf(bestInFamily) == QLatin1String(fam.family)) {
                check(bestInFamily == running.first(),
                      QStringLiteral("%1 at Best keeps the declared order (software first: %2)")
                          .arg(fam.family, running.first()));
            }
        }

        // The reason is the FORMAT and what the destination does with the file. A
        // library name here would be an internal the user cannot act on.
        QString why;
        const QString pick = ctrl::resolveLadderCodec(ctrl::DestClass::Reencode,
                                                      ctrl::QualityBucket::Balanced, &why);
        check(!why.contains(QLatin1String("lib")) && !why.contains(QLatin1String("nvenc"))
                  && !why.contains(QLatin1String("qsv")) && !why.contains(QLatin1String("vaapi")),
              QStringLiteral("the reason names no encoder library (%1)").arg(why));
        check(!why.contains(QLatin1String("s here")) && !why.contains(QLatin1String("budget")),
              QStringLiteral("and promises no time (%1)").arg(why));
        QTextStream(stdout) << "      balanced pick: " << pick << " (" << why << ")\n";
        QTextStream(stdout) << "      running: " << pick << "\n";
    }
}

static uint32_t fourcc(const QString& s) {
    if (s.size() != 4) return 0;
    return (uint32_t)MKTAG(s.toUtf8()[0], s.toUtf8()[1], s.toUtf8()[2], s.toUtf8()[3]);
}

// Interleaved float sine, one buffer for every channel.
static QVector<float> makeAudio(int aFrames, int aFps, int aRate, int aChannels) {
    const int perChannel = (int)((double)aFrames / (double)aFps * (double)aRate);
    QVector<float> out(perChannel * aChannels);
    for (int i = 0; i < perChannel; ++i) {
        const double kPi = 3.14159265358979323846; // M_PI needs _USE_MATH_DEFINES on MSVC
        const float v = 0.25f * (float)std::sin(2.0 * kPi * 440.0 * (double)i / (double)aRate);
        for (int c = 0; c < aChannels; ++c) out[i * aChannels + c] = v;
    }
    return out;
}

// I44: the schema is a LIST of accepted codecs plus a declared default, the
// codec row offers accepted ∩ build-capable, the default follows the row, and
// the user's choice travels through the plan. The review item was "Discord
// codec fixed": a row that merely defaults the codec must leave the choice
// open (contract item 42), and the class policy must never pick past the list.
TEST_SECTION("I44: accepted lists, defaults and the codec row", run_codec_lists) {
    const QList<ctrl::ExportTarget> all =
        ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));
    auto row = [&](const char* id) {
        return ctrl::ExportTargets::find(all, QString::fromLatin1(id));
    };

    // ---- the intersection rule, every row ---------------------------------
    for (const auto& t : all) {
        if (t.artifact != ctrl::ArtifactKind::Video) continue;
        const QList<ctrl::CodecChoice> offered = ctrl::offeredCodecs(t);
        for (const ctrl::CodecChoice& c : offered) {
            check(t.codecs.contains(c.format),
                  QStringLiteral("%1: offered '%2' is accepted by the row").arg(t.id, c.format));
            check(!c.encoder.isEmpty() && !c.label.isEmpty(),
                  QStringLiteral("%1: the %2 entry names its encoder and its label")
                      .arg(t.id, c.format));
            check(c.format == QLatin1String("gif")
                      || media::ffmpeg::encoderUsable(c.encoder.toUtf8().constData()),
                  QStringLiteral("%1: offered encoder %2 runs in this build").arg(t.id, c.encoder));
        }
        for (const QString& format : t.codecs) {
            bool has = false;
            for (const ctrl::CodecChoice& c : offered) {
                if (c.format == format) {
                    has = true;
                    break;
                }
            }
            if (!has)
                QTextStream(stdout) << "      note: " << t.id << " accepts " << format
                                    << " but this build cannot produce it into " << t.container
                                    << "\n";
        }
    }

    // ---- the named rows: the review's case and the facts' own lists --------
    const ctrl::ExportTarget discord = row("chat.discord.file");
    check(discord.codecs == QStringList({QStringLiteral("h264"), QStringLiteral("hevc"),
                                         QStringLiteral("av1")}),
          "discord declares the trio the review names (AV1/H.265/H.264)");
    check(discord.defaultCodec == QLatin1String("h264"), "and declares which one is the default");
    {
        const QList<ctrl::CodecChoice> offered = ctrl::offeredCodecs(discord);
        check(!offered.isEmpty(), "discord offers at least its default codec here");
        for (const ctrl::CodecChoice& c : offered) {
            check(c.label == QStringLiteral("H.264") || c.label == QStringLiteral("H.265")
                      || c.label == QStringLiteral("AV1"),
                  QStringLiteral("the row offers FORMAT names, not libraries (%1)").arg(c.label));
        }
    }
    const ctrl::ExportTarget archive = row("archive.master");
    check(archive.codecs == QStringList({QStringLiteral("ffv1"), QStringLiteral("av1"),
                                         QStringLiteral("hevc"), QStringLiteral("vp9")}),
          "the master row accepts FFV1 / AV1 / H.265 / VP9 (rule decided 2026-10-01)");

    // ---- the default resolution -------------------------------------------
    // The alpha flag is irrelevant outside Archive+non-lossless (the rule
    // decided 2026-10-01); those calls state it explicitly below.
    const ctrl::CodecChoice declared =
        ctrl::resolveDefaultCodec(discord, ctrl::QualityBucket::Balanced, false);
    check(declared.format == discord.defaultCodec,
          "a declared default resolves first, whatever the bucket");
    // A row where the declared default DIVERGES from the policy: reels accepts
    // H.264 and H.265 but declares H.264, while the efficiency policy would
    // take H.265. Without the declared-default rule this resolves to H.265.
    const ctrl::ExportTarget reels = row("post.instagram.reels");
    check(reels.defaultCodec == QLatin1String("h264")
              && reels.codecs.contains(QLatin1String("hevc")),
          "precondition: reels declares H.264 while accepting H.265 too");
    const ctrl::CodecChoice reelsDefault =
        ctrl::resolveDefaultCodec(reels, ctrl::QualityBucket::Best, false);
    check(reelsDefault.format == QLatin1String("h264"),
          "the declared default beats the efficiency policy (Best would otherwise take H.265)");

    const ctrl::ExportTarget website = row("website.video");
    for (const ctrl::QualityBucket bucket :
         {ctrl::QualityBucket::Smaller, ctrl::QualityBucket::Balanced, ctrl::QualityBucket::Best,
          ctrl::QualityBucket::Lossless}) {
        const ctrl::CodecChoice def = ctrl::resolveDefaultCodec(website, bucket, false);
        check(def.format == QLatin1String("h264"),
              QStringLiteral("direct playback stays H.264 at every bucket, list-constrained (%1)")
                  .arg(def.format));
    }

    // ---- the archive rule (user's call 2026-10-01) -------------------------
    // Alpha capability MEASURED in this build (adjudication/alpha_probe: one
    // real encode per codec through media::VideoEncoder, decoded back): ffv1
    // and libvpx-vp9 carry yuva420p; libsvtav1 accepts the request but stores
    // NO alpha at all (silent fallback, all four containers); libx265 refuses
    // it outright. So the transparency-ON pick is VP9 and the offered list
    // carries it.
    const bool hevcRuns = media::ffmpeg::encoderUsable("libx265")
                          || media::ffmpeg::encoderUsable("hevc_nvenc")
                          || media::ffmpeg::encoderUsable("hevc_qsv")
                          || media::ffmpeg::encoderUsable("hevc_vaapi");
    const bool av1Runs = media::ffmpeg::encoderUsable("libsvtav1")
                         || media::ffmpeg::encoderUsable("libaom-av1");
    const bool vp9Runs = media::ffmpeg::encoderUsable("libvpx-vp9");
    const bool ffv1Runs = media::ffmpeg::encoderUsable("ffv1");
    {
        const QList<ctrl::CodecChoice> offered = ctrl::offeredCodecs(archive);
        QStringList formats;
        for (const ctrl::CodecChoice& c : offered) formats << c.format;
        QStringList want;
        if (ffv1Runs) want << QStringLiteral("ffv1");
        if (av1Runs) want << QStringLiteral("av1");
        if (hevcRuns) want << QStringLiteral("hevc");
        if (vp9Runs) want << QStringLiteral("vp9");
        check(formats == want,
              QStringLiteral("the master row offers exactly FFV1/AV1/H.265/VP9 in order (%1)")
                  .arg(formats.join(QLatin1Char('/'))));
    }
    if (ffv1Runs) {
        for (const bool alphaOn : {true, false}) {
            const ctrl::CodecChoice c =
                ctrl::resolveDefaultCodec(archive, ctrl::QualityBucket::Lossless, alphaOn);
            check(c.format == QLatin1String("ffv1"),
                  QStringLiteral("Lossless writes FFV1 whatever the transparency toggle (on=%1)")
                      .arg(alphaOn ? "true" : "false"));
        }
    }
    if (vp9Runs) {
        for (const ctrl::QualityBucket bucket :
             {ctrl::QualityBucket::Smaller, ctrl::QualityBucket::Balanced,
              ctrl::QualityBucket::Best}) {
            const ctrl::CodecChoice c = ctrl::resolveDefaultCodec(archive, bucket, true);
            check(c.format == QLatin1String("vp9"),
                  QStringLiteral(
                      "transparency ON takes the alpha-capable lossy codec (VP9, bucket %1)")
                      .arg(static_cast<int>(bucket)));
        }
    }
    // Transparency OFF: AV1 at EVERY non-Lossless bucket. The ladder's family
    // order (quality per bit: AV1, then H.265, then H.264) is the same at every
    // bucket, and the transparency flag no longer reorders this row's families
    // (user's call 2026-10-01, "AV1 all the way makes more sense"): H.265 is
    // only the fallback for a build with no usable AV1 encoder.
    for (const ctrl::QualityBucket bucket :
         {ctrl::QualityBucket::Smaller, ctrl::QualityBucket::Balanced, ctrl::QualityBucket::Best}) {
        const ctrl::CodecChoice c = ctrl::resolveDefaultCodec(archive, bucket, false);
        if (av1Runs) {
            check(c.format == QLatin1String("av1"),
                  QStringLiteral("transparency OFF takes AV1 at every size bucket (bucket %1)")
                      .arg(static_cast<int>(bucket)));
        } else if (hevcRuns) {
            check(c.format == QLatin1String("hevc"),
                  QStringLiteral("with no AV1 encoder, transparency OFF is H.265 (bucket %1)")
                      .arg(static_cast<int>(bucket)));
        }
    }

    const ctrl::ExportTarget youtube = row("youtube.video");
    const ctrl::CodecChoice youtubeLossless =
        ctrl::resolveDefaultCodec(youtube, ctrl::QualityBucket::Lossless, false);
    check(youtubeLossless.format != QLatin1String("ffv1")
              && youtube.codecs.contains(youtubeLossless.format),
          "YouTube no longer receives FFV1 at Lossless: the policy picks only what the row accepts");
    if (av1Runs) {
        const ctrl::CodecChoice youtubeBest =
            ctrl::resolveDefaultCodec(youtube, ctrl::QualityBucket::Best, false);
        check(youtubeBest.format == QLatin1String("av1"),
              "the efficiency buckets still lead with AV1 where the row accepts it");
    }

    // ---- the user's choice reaches the plan -------------------------------
    ctrl::ExportRequest req;
    req.sourceSize = QSize(640, 360);
    req.selection = ctrl::FrameSelection::fromRange(0, 29);
    req.fps = 24;

    // The archive rule through planExport: the TRANSPARENCY TOGGLE reaches it
    // (the request carries it), and the user's pin still wins over it.
    {
        ctrl::ExportRequest aReq = req;
        aReq.allowTransparency = false;
        const ctrl::ExportPlan off =
            ctrl::planExport(archive, aReq, ctrl::QualityBucket::Balanced);
        if (av1Runs)
            check(off.ok() && ctrl::formatKeyForEncoder(off.config.codec) == QLatin1String("av1"),
                  "the toggle OFF reaches the rule through planExport (AV1 at Balanced)");
        else if (hevcRuns)
            check(off.ok() && ctrl::formatKeyForEncoder(off.config.codec) == QLatin1String("hevc"),
                  "with no AV1 encoder the toggle OFF reaches H.265 at Balanced");
        aReq.allowTransparency = true;
        const ctrl::ExportPlan on =
            ctrl::planExport(archive, aReq, ctrl::QualityBucket::Balanced);
        if (vp9Runs)
            check(on.ok() && ctrl::formatKeyForEncoder(on.config.codec) == QLatin1String("vp9"),
                  "the toggle ON reaches the rule through planExport (VP9 at Balanced)");
        aReq.codec = QStringLiteral("libx265");
        if (hevcRuns) {
            const ctrl::ExportPlan pinned =
                ctrl::planExport(archive, aReq, ctrl::QualityBucket::Balanced);
            check(pinned.ok() && pinned.config.codec == QLatin1String("libx265"),
                  "the user's pinned codec wins over the archive rule");
        }
    }

    const QList<ctrl::CodecChoice> offered = ctrl::offeredCodecs(discord);
    if (offered.size() > 1) {
        req.codec = offered.at(1).encoder;
        const ctrl::ExportPlan plan =
            ctrl::planExport(discord, req, ctrl::QualityBucket::Balanced);
        check(plan.ok() && plan.config.codec == offered.at(1).encoder,
              QStringLiteral("the codec row's choice reaches the plan (%1)")
                  .arg(offered.at(1).encoder));
    }

    // a codec the destination does not accept is refused, not smuggled in:
    req.codec = QStringLiteral("ffv1");
    {
        const ctrl::ExportPlan plan =
            ctrl::planExport(discord, req, ctrl::QualityBucket::Balanced);
        check(plan.ok() && ctrl::formatKeyForEncoder(plan.config.codec) == QLatin1String("h264"),
              "a codec the destination refuses falls back to its declared default");
    }

    // the passthrough keeps the EXACT encoder of an accepted format (the plan's
    // own resolution may name hardware; the request must not downgrade it):
    if (media::ffmpeg::encoderUsable("libx264")) {
        req.codec = QStringLiteral("libx264");
        const ctrl::ExportPlan plan =
            ctrl::planExport(discord, req, ctrl::QualityBucket::Balanced);
        check(plan.ok() && plan.config.codec == QLatin1String("libx264"),
              "an exact accepted encoder passes through untouched");
    }

    // ---- hvc1 is gated on the RESOLVED family (ruling 2026-10-03) ----------
    // Every ISO row that offers HEVC must ASK for the hvc1 tag, and must say so
    // with the qualifier rather than by offering HEVC over the other families.
    for (const char* id : {"chat.discord.file", "post.instagram.reels",
                           "post.pinterest.video", "youtube.video", "website.video",
                           "generic.file"}) {
        const ctrl::ExportTarget t = row(id);
        check(t.container == QLatin1String("mp4") && t.codecs.contains(QLatin1String("hevc")),
              QStringLiteral("%1: mp4 offering HEVC, so hvc1 is meaningful there")
                  .arg(QLatin1String(id)));
        check(t.codecTag == QLatin1String("hvc1")
                  && t.codecTagCodec == QLatin1String("hevc"),
              QStringLiteral("%1: asks for hvc1, qualified to hevc (tag='%2' codec='%3')")
                  .arg(QLatin1String(id), t.codecTag, t.codecTagCodec));
    }
    // The one row whose codec list is a single family keeps the UNQUALIFIED form:
    // the gate must not need a qualifier to write the tag it always wrote.
    {
        const ctrl::ExportTarget safari = row("safari.transparent");
        check(safari.codecTag == QLatin1String("hvc1") && safari.codecTagCodec.isEmpty(),
              "safari.transparent: hvc1 stays unconditional (H.265 is its only codec)");
    }

    // ... and the gate itself, at the planner: the SAME row and bucket, asked for
    // H.265 and then H.264, carries the tag and then does not. This is what makes
    // "H.264 output is unchanged" an assertion instead of a promise.
    if (media::ffmpeg::encoderUsable("libx265") && media::ffmpeg::encoderUsable("libx264")) {
        ctrl::ExportRequest aReq = req;
        aReq.codec = QStringLiteral("libx265");
        const ctrl::ExportPlan hevcPlan =
            ctrl::planExport(youtube, aReq, ctrl::QualityBucket::Balanced);
        check(hevcPlan.ok()
                  && ctrl::formatKeyForEncoder(hevcPlan.config.codec) == QLatin1String("hevc"),
              QStringLiteral("hvc1 gate: the HEVC request really resolves to HEVC ('%1')")
                  .arg(hevcPlan.config.codec));
        check(hevcPlan.ok() && hevcPlan.config.codecTag == QLatin1String("hvc1"),
              QStringLiteral("hvc1 gate: HEVC in mp4 carries hvc1 (got '%1')")
                  .arg(hevcPlan.config.codecTag));
        aReq.codec = QStringLiteral("libx264");
        const ctrl::ExportPlan h264Plan =
            ctrl::planExport(youtube, aReq, ctrl::QualityBucket::Balanced);
        check(h264Plan.ok()
                  && ctrl::formatKeyForEncoder(h264Plan.config.codec) == QLatin1String("h264"),
              QStringLiteral("hvc1 gate: the H.264 request really resolves to H.264 ('%1')")
                  .arg(h264Plan.config.codec));
        check(h264Plan.ok() && h264Plan.config.codecTag.isEmpty(),
              QStringLiteral("hvc1 gate: H.264 in mp4 carries NO codec tag (got '%1')")
                  .arg(h264Plan.config.codecTag));
    } else {
        QTextStream(stdout) << "      SKIP  hvc1 gate: libx265 or libx264 is not usable in this "
                               "build, so the planner pair cannot be exercised\n";
    }
}

// Codecs whose decoded alpha we can actually observe in-process. WebP's
// libwebp decoder cannot read ANIMATED WebP, and HEVC alpha is not surfaced
// through the native decoder, so those rows skip the alpha assertion (rather
// than asserting something the probe cannot see).
static bool alphaObservable(const QString& aCodec) {
    return aCodec == QLatin1String("libvpx-vp9") || aCodec == QLatin1String("apng") ||
           aCodec == QLatin1String("ffv1");
}

TEST_SECTION("destination conformance (every row)", run_conformance) {

    const QList<ctrl::ExportTarget> all =
        ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));

    for (const ctrl::ExportTarget& t : all) {
        // GIF rows go through media::GifEncoder rather than an encoder config, which
        // is why they used to be skipped here with a "routed elsewhere" pass — a
        // check that could not fail. They are encoded for real instead: three rows
        // (Twitch, WeChat, QQ) state exact sticker sizes, and a size rule nothing
        // exercises is a size rule that can silently stop being applied.
        if (t.container.compare(QLatin1String("gif"), Qt::CaseInsensitive) == 0) {
            const QString gout = aeTestTempPath(QStringLiteral("ae_conf_%1.gif")
                                                           .arg(QString(t.id).replace(QLatin1Char('.'),
                                                                                      QLatin1Char('_'))));
            ctrl::ExportRequest greq;
            greq.outputPath = gout;
            greq.sourceSize = QSize(640, 480); // deliberately non-square
            greq.fps = 30;
            greq.selection = ctrl::FrameSelection::fromCount(3);
            const ctrl::ExportPlan gplan = ctrl::planExport(t, greq, ctrl::QualityBucket::Balanced);
            check(gplan.ok(), QStringLiteral("%1: GIF plan builds (%2)").arg(t.id, gplan.error));
            if (!gplan.ok()) continue;
            QList<QImage> gframes;
            for (int i = 0; i < gplan.frames(); ++i) {
                const QImage f = makeFrame(gplan.renderSize.width(), gplan.renderSize.height(), i);
                gframes.append(f.size() == gplan.size
                                   ? f
                                   : media::fitImage(f, gplan.size, gplan.config.fit));
            }
            media::GifEncoder gif;
            media::GifEncoder::Config gcfg;
            gcfg.outputPath = gout;
            gcfg.fps = gplan.fps > 0 ? gplan.fps : 15;
            gcfg.transparent = gplan.alphaEnabled;
            QString gerr;
            check(gif.encode(gframes, gcfg, &gerr),
                  QStringLiteral("%1: GIF encodes (%2)").arg(t.id, gerr));
            int gw = 0, gh = 0;
            const int gn = decodeInfo(gout, &gw, &gh);
            check(gn > 0 && gw == gplan.size.width() && gh == gplan.size.height(),
                  QStringLiteral("%1: GIF decodes back at the planned size %2x%3 (got %4x%5, %6 frames)")
                      .arg(t.id)
                      .arg(gplan.size.width())
                      .arg(gplan.size.height())
                      .arg(gw)
                      .arg(gh)
                      .arg(gn));
            QFile::remove(gout);
            continue;
        }

        // Image artifacts produce files through Qt's image writers, so "does it
        // encode" is the wrong question; "does it write the files it promises" is
        // the right one. A still must leave exactly ONE file at the planned size,
        // and a sequence must leave the planned number of them.
        if (t.artifact != ctrl::ArtifactKind::Video) {
            ctrl::ExportRequest ireq;
            ireq.sourceSize = QSize(640, 480);
            ireq.fps = 30;
            ireq.selection = ctrl::FrameSelection::fromCount(3);
            ireq.playhead = 1;
            const ctrl::ExportPlan iplan = ctrl::planExport(t, ireq, ctrl::QualityBucket::Balanced);
            check(iplan.ok(), QStringLiteral("%1: image plan builds (%2)").arg(t.id, iplan.error));
            if (!iplan.ok()) continue;

            const QString dir = aeTestTempPath(QStringLiteral("ae_img_%1")
                                                          .arg(QString(t.id).replace(QLatin1Char('.'),
                                                                                     QLatin1Char('_'))));
            QDir().mkpath(dir);
            const QImage frame = makeFrame(iplan.renderSize.width(), iplan.renderSize.height(), 0);
            qint64 total = 0;
            int written = 0;
            const int count = iplan.artifact == ctrl::ArtifactKind::Still ? 1 : iplan.frames();
            for (int i = 0; i < count; ++i) {
                // The exporter's own naming: stem + 4-digit index, or the bare stem
                // for a still (see Exporter::decideImagePath).
                const QString stem =
                    iplan.artifact == ctrl::ArtifactKind::Still
                        ? QStringLiteral("still")
                        : QStringLiteral("frame%1").arg(i, 4, 10, QChar('0'));
                const QString path = dir + QLatin1Char('/') + stem + QLatin1Char('.')
                                     + iplan.imageSuffix;
                if (!frame.save(path)) {
                    check(false, QStringLiteral("%1: could not write %2").arg(t.id, path));
                    break;
                }
                ++written;
                total += QFileInfo(path).size();
                const QImage back(path);
                if (back.isNull() || back.size() != frame.size()) {
                    check(false, QStringLiteral("%1: %2 does not read back at the planned size")
                                     .arg(t.id, QFileInfo(path).fileName()));
                    break;
                }
            }
            check(written == count,
                  QStringLiteral("%1: wrote %2 of %3 %4 files")
                      .arg(t.id)
                      .arg(written)
                      .arg(count)
                      .arg(iplan.artifact == ctrl::ArtifactKind::Still ? QStringLiteral("image")
                                                                       : QStringLiteral("frame")));
            if (t.caps.maxBytes > 0) {
                check(total <= t.caps.maxBytes,
                      QStringLiteral("%1: the written set is inside the byte cap (%2 <= %3)")
                          .arg(t.id)
                          .arg((qlonglong)total)
                          .arg((qlonglong)t.caps.maxBytes));
            }
            QDir(dir).removeRecursively();
            continue;
        }

        const QString out = aeTestTempPath(QStringLiteral("ae_conf_%1.%2")
                                                      .arg(QString(t.id).replace(QLatin1Char('.'), QLatin1Char('_')),
                                                           t.container));

        ctrl::ExportRequest req;
        req.outputPath = out;
        req.sourceSize = QSize(640, 480); // deliberately non-square
        req.fps = 30;
        req.selection = ctrl::FrameSelection::fromCount(3);
        req.exportableAudioTracks = 2; // the project has sound; each row decides if it lands

        const ctrl::ExportPlan plan = ctrl::planExport(t, req, ctrl::QualityBucket::Balanced);
        if (!plan.ok()) {
            // A destination whose codec is platform-specific (HEVC+alpha needs
            // VideoToolbox) cannot always produce a file. The acceptable outcome
            // is a CLEAN, EXPLAINED refusal — never a silent one, and never a
            // crash. Silently producing wrong output is the only failure here.
            check(!plan.error.isEmpty(),
                  QStringLiteral("%1: unavailable here, refused cleanly: %2").arg(t.id, plan.error));
            QTextStream(stdout) << "      " << t.id << " -> UNAVAILABLE: " << plan.error << "\n";
            continue;
        }

        // Render at the plan's RENDER size, exactly as the app would: the
        // encoder is what fits + pads to the output size.
        QList<QImage> frames;
        for (int i = 0; i < plan.frames(); ++i) {
            frames.append(makeFrame(plan.renderSize.width(), plan.renderSize.height(), i));
        }

        media::VideoEncoder enc;
        if (!enc.open(plan.config)) {
            check(false, QStringLiteral("%1: open succeeds (%2)").arg(t.id, enc.error()));
            continue;
        }
        for (const QImage& f : frames) enc.write(f);
        if (plan.config.audioChannels > 0) {
            const QVector<float> pcm =
                makeAudio(plan.frames(), plan.fps, plan.config.audioSampleRate, plan.config.audioChannels);
            enc.setAudio(pcm.constData(), plan.config.audioChannels, plan.config.audioSampleRate,
                         pcm.size() / plan.config.audioChannels);
        }
        if (!enc.close()) {
            check(false, QStringLiteral("%1: encode succeeds (%2)").arg(t.id, enc.error()));
            continue;
        }
        check(QFileInfo(out).size() > 0, QStringLiteral("%1: file written").arg(t.id));

        // VP9 alpha is only visible to the libvpx decoder; the native one drops it.
        const char* forced =
            plan.config.codec == QLatin1String("libvpx-vp9") ? "libvpx-vp9" : nullptr;
        const Probe p = probeFile(out, forced);

        QTextStream(stdout) << "      " << t.id << " -> " << plan.config.codec << " "
                            << plan.size.width() << "x" << plan.size.height()
                            << " alpha=" << (plan.alphaEnabled ? "yes" : "no")
                            << " audio=" << (plan.config.audioChannels > 0 ? "yes" : "no")
                            << " decoded=" << p.decodedPixFmt
                            << " pixFmt=" << plan.config.pixFmt << "\n";

        if (!p.opened || p.frames == 0) {
            // Animated WebP: libav cannot read it back, so the container is read
            // directly instead of skipping the row's assertions entirely.
            const WebpInfo w = readWebpInfo(out);
            QTextStream(stdout) << "        container: webp " << w.width << "x" << w.height
                                << " frames=" << w.frames
                                << " alpha=" << (w.hasAlpha ? "yes" : "no")
                                << " animated=" << (w.animated ? "yes" : "no") << "\n";
            // Verify the verifier: a parser that says "valid" to everything would
            // make every assertion below it meaningless.
            {
                const QString png = aeTestTempPath(QStringLiteral("ae_not_webp.png"));
                QImage(8, 8, QImage::Format_ARGB32).save(png);
                const WebpInfo notWebp = readWebpInfo(png);
                check(!notWebp.valid && notWebp.frames == 0,
                      "the WebP reader rejects a file that is not a WebP (PNG)");
                const WebpInfo missing = readWebpInfo(aeTestTempPath("ae_does_not_exist.webp"));
                check(!missing.valid, "the WebP reader rejects a missing file");
                QFile::remove(png);
            }
            check(w.valid, QStringLiteral("%1: the file is a well-formed WebP container").arg(t.id));
            check(w.animated && w.frames == plan.frames(),
                  QStringLiteral("%1: %2 animation frames, one per planned frame (%3)")
                      .arg(t.id).arg(w.frames).arg(plan.frames()));
            check(w.width == plan.size.width() && w.height == plan.size.height(),
                  QStringLiteral("%1: canvas is %2x%3, as the plan says")
                      .arg(t.id).arg(w.width).arg(w.height));
            // The alpha flag is what Discord and WhatsApp read to decide the
            // sticker has a transparent background.
            check(w.hasAlpha == plan.alphaEnabled,
                  QStringLiteral("%1: alpha flag = %2 (the plan says %3)")
                      .arg(t.id)
                      .arg(w.hasAlpha ? "set" : "clear")
                      .arg(plan.alphaEnabled ? "enabled" : "disabled"));

            // Verify the verifier before trusting it: a blank animation must
            // measure 0% ink, or "ink > 0" below could never fail.
            if (magickAvailable()) {
                const QString blank = aeTestTempPath(QStringLiteral("ae_blank_anim.webp"));
                const QString mk = QStringLiteral(
                    "magick -size 32x32 xc:none -delay 5 xc:none -loop 0 \"%1\" 2>/dev/null").arg(blank);
                FILE* mp = popen(mk.toUtf8().constData(), "r");
                if (mp) pclose(mp);
                const MagickInfo b = magickRead(blank);
                check(b.ok && b.inkPercent == 0.0,
                      QStringLiteral("a fully transparent animation measures 0% ink (got %1)")
                          .arg(b.inkPercent, 0, 'f', 1));
                QFile::remove(blank);
            }

            // And the PIXELS. Structure cannot tell a sticker from a correctly
            // sized container of empty frames, which is exactly the failure this
            // session kept finding elsewhere.
            const MagickInfo m = magickRead(out);
            if (!m.available) {
                QTextStream(stdout) << "        SKIP  pixel content: " << m.error << "\n";
            } else {
                QTextStream(stdout) << "        decoded: " << m.frames << " frames "
                                    << m.width << "x" << m.height
                                    << " ink=" << QString::number(m.inkPercent, 'f', 1) << "%\n";
                check(m.ok, QStringLiteral("%1: the frames decode (%2)").arg(t.id, m.error));
                if (m.ok) {
                    check(m.frames == plan.frames() && m.width == plan.size.width()
                              && m.height == plan.size.height(),
                          QStringLiteral("%1: %2 frames at %3x%4, as planned")
                              .arg(t.id).arg(m.frames).arg(m.width).arg(m.height));
                    check(m.inkPercent > 0.0,
                          QStringLiteral("%1: the frames contain the artwork (%2% ink)")
                              .arg(t.id).arg(m.inkPercent, 0, 'f', 1));
                }
            }
            continue;
        }

        check(p.width == plan.size.width() && p.height == plan.size.height(),
              QStringLiteral("%1: decoded dims %2x%3 match the plan")
                  .arg(t.id)
                  .arg(p.width)
                  .arg(p.height));

        // Audio capability is enforced, not advisory.
        const int expectedAudio = plan.config.audioChannels > 0 ? 1 : 0;
        check(p.audioStreams == expectedAudio,
              QStringLiteral("%1: audio streams = %2 (expected %3; capability=%4)")
                  .arg(t.id)
                  .arg(p.audioStreams)
                  .arg(expectedAudio)
                  .arg(plan.audioEnabled ? "allowed" : "forbidden"));

        // Alpha: required rows must carry a varying alpha; forbidden rows must not.
        if (alphaObservable(plan.config.codec)) {
            if (plan.alphaEnabled) {
                check(p.minAlpha < p.maxAlpha,
                      QStringLiteral("%1: alpha varies (min=%2 max=%3) as required")
                          .arg(t.id)
                          .arg(p.minAlpha)
                          .arg(p.maxAlpha));
            } else {
                check(p.minAlpha == p.maxAlpha,
                      QStringLiteral("%1: no alpha channel (min==max==%2) as required")
                          .arg(t.id)
                          .arg(p.minAlpha));
            }
        }

        // Stream codec tag (hvc1) must actually land in the container — and only
        // when the plan resolved the codec FAMILY that the tag is qualified to
        // (ruling 2026-10-03: tag HEVC in an ISO container; nothing else moves).
        // An element without the qualifier stays unconditional: safari.transparent
        // offers H.265 alone, so there the condition cannot arise.
        if (!t.codecTag.isEmpty()) {
            const QString resolvedFamily = ctrl::formatKeyForEncoder(plan.config.codec);
            const bool applies = t.codecTagCodec.isEmpty() || t.codecTagCodec == resolvedFamily;
            if (applies) {
                check(p.codecTag == fourcc(t.codecTag),
                      QStringLiteral("%1: stream codec tag is '%2' (got 0x%3)")
                          .arg(t.id, t.codecTag)
                          .arg(p.codecTag, 0, 16));
            } else {
                // Not "== 0": the muxer writes its OWN tag for the codec it wrote
                // (avc1, av01, hev1). What must not happen is the qualified one.
                check(p.codecTag != fourcc(t.codecTag),
                      QStringLiteral("%1: tag '%2' is NOT written for '%3' (family '%4', "
                                     "tag qualified to '%5'; container wrote 0x%6)")
                          .arg(t.id, t.codecTag, plan.config.codec, resolvedFamily,
                               t.codecTagCodec)
                          .arg(p.codecTag, 0, 16));
            }
        }
    }
}

// A misspelt encoder option must be an ERROR, not a silently-ignored typo:
// "auto-alt-ref" typo'd means broken alpha with no other symptom.
TEST_SECTION("unknown encoder option is rejected", run_option_typo_is_fatal) {

    media::VideoEncoder::Config cfg;
    cfg.outputPath = aeTestTempPath("ae_badopt.webm");
    cfg.container = QStringLiteral("webm");
    cfg.codec = QStringLiteral("libvpx-vp9");
    cfg.pixFmt = QStringLiteral("yuva420p");
    cfg.transparent = true;
    cfg.width = 64;
    cfg.height = 64;
    cfg.fps = 24;
    cfg.encOptions.insert(QStringLiteral("auto-alt-reff"), QStringLiteral("0")); // typo

    media::VideoEncoder enc;
    const bool opened = enc.open(cfg);
    check(!opened, "a typo'd encoder option fails open() instead of being ignored");
    if (!opened) QTextStream(stdout) << "      error: " << enc.error() << "\n";

    // ...and the correctly-spelled option still works.
    media::VideoEncoder::Config ok = cfg;
    ok.encOptions.clear();
    ok.encOptions.insert(QStringLiteral("auto-alt-ref"), QStringLiteral("0"));
    media::VideoEncoder enc2;
    check(enc2.open(ok), "the correctly-spelled option opens");
    enc2.close();
}

TEST_SECTION("audio is derived, not requested", run_audio_is_derived) {

    const QList<ctrl::ExportTarget> all =
        ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));
    const ctrl::ExportTarget yt =
        ctrl::ExportTargets::find(all, QStringLiteral("youtube.video"));
    const ctrl::ExportTarget tg =
        ctrl::ExportTargets::find(all, QStringLiteral("telegram.sticker"));

    // Audio is not a preference: it is included iff the project HAS audio, the
    // user did not mute, and the destination permits it. Encode each case and
    // count the actual audio streams in the produced file.
    auto audioStreams = [&](const ctrl::ExportTarget& t, bool aProjectHasAudio, bool aMute) -> int {
        ctrl::ExportRequest req;
        req.outputPath = aeTestTempPath(QStringLiteral("ae_audio.%1").arg(t.container));
        req.sourceSize = QSize(320, 240);
        req.fps = 30;
        req.selection = ctrl::FrameSelection::fromCount(3);
        req.exportableAudioTracks = aProjectHasAudio ? 2 : 0;
        req.mute = aMute;

        const ctrl::ExportPlan plan =
            ctrl::planExport(t, req, ctrl::QualityBucket::Balanced);
        if (!plan.ok()) return -1;

        QList<QImage> frames;
        for (int i = 0; i < plan.frames(); ++i) {
            frames.append(makeFrame(plan.renderSize.width(), plan.renderSize.height(), i));
        }

        media::VideoEncoder enc;
        if (!enc.open(plan.config)) return -1;
        for (const QImage& f : frames) enc.write(f);
        if (plan.config.audioChannels > 0) {
            const QVector<float> pcm = makeAudio(plan.frames(), plan.fps,
                                                 plan.config.audioSampleRate,
                                                 plan.config.audioChannels);
            enc.setAudio(pcm.constData(), plan.config.audioChannels,
                         plan.config.audioSampleRate,
                         pcm.size() / plan.config.audioChannels);
        }
        if (!enc.close()) return -1;
        return probeFile(req.outputPath).audioStreams;
    };

    check(audioStreams(yt, true, false) == 1,
          "project has audio + destination permits -> 1 audio stream");
    check(audioStreams(yt, true, true) == 0, "muted -> no audio stream");
    check(audioStreams(yt, false, false) == 0,
          "silent project (or all tracks reference-only) -> no audio stream");
    check(audioStreams(tg, true, false) == 0,
          "destination that forbids audio -> no audio stream");

    // And the derived decision must be visible, not silent.
    ctrl::ExportRequest noteReq;
    noteReq.sourceSize = QSize(640, 480);
    noteReq.exportableAudioTracks = 2;
    const ctrl::ExportPlan notePlan =
        ctrl::planExport(tg, noteReq, ctrl::QualityBucket::Balanced);
    const bool explained = notePlan.notes.filter(QStringLiteral("No sound")).size() == 1;
    check(explained, "a destination leaving sound out says so in the plan notes");

    // Audio parameters are per-destination, and the container still wins for
    // the codec: WebM cannot carry AAC.
    ctrl::ExportRequest ar;
    ar.sourceSize = QSize(640, 480);
    ar.exportableAudioTracks = 1;
    const ctrl::ExportPlan ytPlan =
        ctrl::planExport(yt, ar, ctrl::QualityBucket::Balanced);
    check(ytPlan.config.audioCodec == QLatin1String("aac") &&
              ytPlan.config.audioBitrate == 256000,
          QStringLiteral("YouTube gets its declared audio params (%1 @ %2)")
              .arg(ytPlan.config.audioCodec)
              .arg(ytPlan.config.audioBitrate));
    const ctrl::ExportPlan obsPlan =
        ctrl::planExport(ctrl::ExportTargets::find(all, QStringLiteral("obs.overlay")), ar,
                         ctrl::QualityBucket::Balanced);
    check(obsPlan.config.audioCodec == QLatin1String("libopus"),
          QStringLiteral("a WebM destination gets Opus, not AAC (got %1)")
              .arg(obsPlan.config.audioCodec));
    const ctrl::ExportPlan arcPlan =
        ctrl::planExport(ctrl::ExportTargets::find(all, QStringLiteral("archive.master")), ar,
                         ctrl::QualityBucket::Balanced);
    check(arcPlan.config.audioCodec == QLatin1String("flac"),
          "an archive master keeps audio lossless (flac)");
}

// The artifact list the escape hatch offers when the user has no destination in
// mind. Every claim it makes is checked against the probe, because the previous
// answer to "which formats can I export?" was a text file that listed AVI,
// Huffyuv, Ogg and Theora as though every build had them.
TEST_SECTION("artifact list is derived, and every entry is real", run_artifacts) {
    const QList<media::ffmpeg::ContainerOption> containers = media::ffmpeg::writableContainers();
    QTextStream out(stdout);
    out << "    containers this build can write: " << containers.size() << "\n";
    for (const media::ffmpeg::ContainerOption& c : containers) {
        out << "      " << c.name.leftJustified(7) << c.label.leftJustified(20) << " -> "
            << c.defaultCodec.leftJustified(14) << " alpha=" << (c.alphaCapable ? "yes" : "no")
            << "\n";
    }

    check(!containers.isEmpty(), "at least one container is offerable");
    bool hasMp4 = false;
    bool hasGif = false;
    for (const media::ffmpeg::ContainerOption& c : containers) {
        if (c.name == QLatin1String("mp4")) hasMp4 = true;
        if (c.name == QLatin1String("gif")) hasGif = true;
        check(!c.label.isEmpty(), QStringLiteral("%1 has a label").arg(c.name));
        if (c.name == QLatin1String("gif")) {
            // media::GifEncoder, not libav: present whenever the app is.
            check(c.defaultCodec == QLatin1String("gif"), "gif routes to the in-process GifEncoder");
        } else {
            check(media::ffmpeg::containerAcceptsUsableCodec(c.name, c.defaultCodec),
                  QStringLiteral("%1's default (%2) really runs and is really muxable here")
                      .arg(c.name, c.defaultCodec));
        }
    }
    check(hasMp4, "mp4 is offerable (the one container everything plays)");
    check(hasGif, "gif is offerable (the one format every chat accepts)");

    // The codec list narrows to what the container can carry: H.264 in WebM is
    // impossible, and offering it would produce a confusing mux failure.
    const QList<media::ffmpeg::CodecOption> mp4 = media::ffmpeg::usableCodecsForContainer(QStringLiteral("mp4"));
    out << "    mp4 codecs: " << mp4.size() << "\n";
    for (const media::ffmpeg::CodecOption& c : mp4) {
        out << "      " << c.encoder.leftJustified(14) << c.label.leftJustified(22)
            << " lossless=" << (c.lossless ? "yes" : "no")
            << " alpha=" << (c.alphaCapable ? "yes" : "no") << "\n";
    }
    check(!mp4.isEmpty(), "mp4 offers at least one codec");
    for (const media::ffmpeg::CodecOption& c : mp4) {
        check(!c.label.isEmpty(), QStringLiteral("%1 is labelled").arg(c.encoder));
        check(media::ffmpeg::containerAcceptsUsableCodec(QStringLiteral("mp4"), c.encoder),
              QStringLiteral("offered codec %1 is usable + muxable").arg(c.encoder));
    }
    // Every codec each container offers, so the escape hatch's reach is visible
    // in the test output rather than assumed: this list is what has to keep the
    // legacy menu's promise (ProRes for an editor, lossless FFV1 for a master,
    // Theora/AVI for anyone who still needs them) without a hand-written table.
    for (const media::ffmpeg::ContainerOption& c : containers) {
        if (c.name == QLatin1String("gif"))
            continue; // media::GifEncoder, not a libav codec choice
        QStringList names;
        for (const media::ffmpeg::CodecOption& codec : media::ffmpeg::usableCodecsForContainer(c.name))
            names << codec.encoder;
        out << "      " << c.name.leftJustified(7) << "offers: " << names.join(QLatin1String(", ")) << "\n";
    }

    bool webmOffersH264 = false;
    for (const media::ffmpeg::CodecOption& c : media::ffmpeg::usableCodecsForContainer(QStringLiteral("webm"))) {
        if (c.encoder == QLatin1String("libx264")) webmOffersH264 = true;
    }
    check(!webmOffersH264, "H.264 in WebM is not offered (the muxer cannot carry it)");

    // The capabilities the deprecated menu existed to reach must remain
    // reachable, where this build can produce them at all — otherwise deleting
    // it would be a silent loss.
    auto offers = [](const QString& aContainer, const QString& aEncoder) {
        for (const media::ffmpeg::CodecOption& c : media::ffmpeg::usableCodecsForContainer(aContainer)) {
            if (c.encoder == aEncoder && media::ffmpeg::containerAcceptsUsableCodec(aContainer, aEncoder))
                return true;
        }
        return false;
    };
    if (media::ffmpeg::encoderUsable("prores_ks")) {
        check(offers(QStringLiteral("mov"), QStringLiteral("prores_ks")),
              "ProRes 4444 in MOV stays reachable (the editor handoff, with alpha)");
    }
    if (media::ffmpeg::encoderUsable("ffv1")) {
        check(offers(QStringLiteral("mkv"), QStringLiteral("ffv1")),
              "FFV1 in MKV stays reachable (the lossless master)");
    }
    if (media::ffmpeg::encoderUsable("utvideo")) {
        check(offers(QStringLiteral("avi"), QStringLiteral("utvideo")),
              "Ut Video in AVI stays reachable (the old lossless/alpha pair)");
    }
    check(media::ffmpeg::usableCodecsForContainer(QStringLiteral("no_such_container")).isEmpty(),
          "an unknown container offers nothing rather than a fabricated default");
}

// The Quality selector has to reach the encoder for EVERY destination, not just
// the ones whose codec comes from the ladder. Ten of the fourteen rows fix the
// codec (Telegram, Discord, WhatsApp, LINE, OBS...), so for those the bucket used
// to change nothing at all: "Smaller file" and "Best quality" emitted identical
// instructions and produced near-identical files.
TEST_SECTION("quality intent reaches the encoder", run_bucket_quality) {
    const QList<ctrl::ExportTarget> targets =
        ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));
    const ctrl::ExportTarget telegram =
        ctrl::ExportTargets::find(targets, QStringLiteral("telegram.sticker"));
    check(telegram.defaultCodec == QLatin1String("vp9")
              && telegram.codecs == QStringList{QStringLiteral("vp9")},
          "telegram.sticker dictates its codec (the case that was inert): one accepted, declared");

    ctrl::ExportRequest req;
    req.sourceSize = QSize(512, 512);
    req.selection = ctrl::FrameSelection::fromCount(30);
    req.fps = 24;

    auto crfFor = [&](ctrl::QualityBucket aBucket) -> QString {
        const ctrl::ExportPlan plan = ctrl::planExport(telegram, req, aBucket);
        if (!plan.ok()) return QStringLiteral("<plan error: %1>").arg(plan.error);
        return plan.config.encOptions.value(QStringLiteral("crf"), QStringLiteral("<absent>"));
    };
    const QString smaller = crfFor(ctrl::QualityBucket::Smaller);
    const QString balanced = crfFor(ctrl::QualityBucket::Balanced);
    const QString best = crfFor(ctrl::QualityBucket::Best);
    QTextStream(stdout) << "    telegram.sticker crf: smaller=" << smaller
                        << " balanced=" << balanced << " best=" << best << "\n";

    check(smaller != QStringLiteral("<absent>"),
          "the fixed-codec destination gets a quality knob at all");
    check(smaller != best, "Smaller and Best are no longer the same instruction");
    check(balanced != best, "Balanced differs from Best");

    // Direction: VP9's knob is CRF, where a LARGER number means a smaller file.
    // Asserting the direction and not merely inequality is what stops this test
    // from passing if the mapping is ever reversed.
    check(smaller.toInt() > balanced.toInt() && balanced.toInt() > best.toInt(),
          QStringLiteral("CRF rises as the intent gets smaller-quality (%1 > %2 > %3)")
              .arg(smaller, balanced, best));

    // And inside the span the data declares, or the fit-to-cap loop would start
    // outside the range it is allowed to search.
    const QHash<QString, ctrl::QualityControl> qc =
        ctrl::ExportTargets::loadQualityControl(QStringLiteral(AE_EXPORT_TARGETS_PATH));
    const ctrl::QualityControl control = qc.value(QStringLiteral("libvpx-vp9"));
    check(control.option == QLatin1String("crf") && control.best == 24 && control.worst == 52,
          "the span for libvpx-vp9 comes from the table");
    for (const QString& v : {smaller, balanced, best}) {
        check(v.toInt() >= control.best && v.toInt() <= control.worst,
              QStringLiteral("crf %1 stays inside [%2, %3]").arg(v).arg(control.best).arg(control.worst));
    }

    // A codec with no quality knob must NOT be given a fabricated one: FFV1 is
    // lossless, and inventing a "crf" for it would be a lie that the encoder
    // then rejects as an unknown option.
    const ctrl::ExportTarget master =
        ctrl::ExportTargets::find(targets, QStringLiteral("archive.master"));
    const ctrl::ExportPlan losslessPlan =
        ctrl::planExport(master, req, ctrl::QualityBucket::Lossless);
    check(losslessPlan.ok(), QStringLiteral("lossless master plan builds (%1)").arg(losslessPlan.error));
    check(losslessPlan.config.encOptions.value(QStringLiteral("crf"), QString()).isEmpty(),
          "a lossless codec is left alone (no invented quality knob)");

    // Asking a lossy codec for "Lossless" cannot be honoured, and the plan says
    // so rather than quietly pretending.
    const ctrl::ExportPlan losslessOnVp9 =
        ctrl::planExport(telegram, req, ctrl::QualityBucket::Lossless);
    bool saidSo = false;
    for (const QString& n : losslessOnVp9.notes) {
        if (n.contains(QLatin1String("lossy"), Qt::CaseInsensitive)) saidSo = true;
    }
    check(saidSo, "Lossless on a lossy codec is stated, not silently approximated");

    // The option being SET is not the claim; the claim is that the file changes.
    // Encode real frames both ways through the same code path the app uses and
    // compare the output — the whole point of this session's fixes is that a
    // setting which does not reach the output is worse than no setting.
    {
        const ctrl::ExportTarget generic =
            ctrl::ExportTargets::find(targets, QStringLiteral("generic.file"));
        const QString smallPath = aeTestTempPath(QStringLiteral("ae_bucket_small.mp4"));
        const QString bestPath = aeTestTempPath(QStringLiteral("ae_bucket_best.mp4"));
        auto encodeAt = [&](ctrl::QualityBucket aBucket, const QString& aPath) -> qint64 {
            ctrl::ExportRequest r = req;
            r.outputPath = aPath;
            r.sourceSize = QSize(320, 240);
            r.selection = ctrl::FrameSelection::fromCount(8);
            ctrl::ExportPlan p = ctrl::planExport(generic, r, aBucket);
            if (!p.ok()) return -1;
            p.config.outputPath = aPath;
            QList<QImage> frames;
            for (int i = 0; i < p.frames(); ++i)
                frames.append(makeFrame(p.renderSize.width(), p.renderSize.height(), i));
            media::VideoEncoder enc;
            if (!enc.open(p.config) || !enc.writeAll(frames, p.config)) return -1;
            return QFileInfo(aPath).size();
        };
        const qint64 smallBytes = encodeAt(ctrl::QualityBucket::Smaller, smallPath);
        const qint64 bestBytes = encodeAt(ctrl::QualityBucket::Best, bestPath);
        QTextStream(stdout) << "    generic.file: smaller=" << smallBytes
                            << " bytes, best=" << bestBytes << " bytes\n";
        check(smallBytes > 0 && bestBytes > 0, "both buckets encode");
        check(bestBytes > smallBytes,
              QStringLiteral("Best really produces a larger file than Smaller (%1 > %2)")
                  .arg(bestBytes).arg(smallBytes));
        QFile::remove(smallPath);
        QFile::remove(bestPath);
    }

    // libwebp's knob runs the OTHER way (0-100, higher is better), so the same
    // mapping must produce the opposite ordering. One table, both directions.
    const ctrl::ExportTarget discordEmoji =
        ctrl::ExportTargets::find(targets, QStringLiteral("discord.emoji"));
    const ctrl::ExportPlan webpSmall =
        ctrl::planExport(discordEmoji, req, ctrl::QualityBucket::Smaller);
    const ctrl::ExportPlan webpBest =
        ctrl::planExport(discordEmoji, req, ctrl::QualityBucket::Best);
    const QString ws = webpSmall.config.encOptions.value(QStringLiteral("quality"));
    const QString wb = webpBest.config.encOptions.value(QStringLiteral("quality"));
    QTextStream(stdout) << "    discord.emoji webp quality: smaller=" << ws << " best=" << wb << "\n";
    check(!ws.isEmpty() && !wb.isEmpty(), "the WebP rows get their quality knob too");
    check(ws.toInt() < wb.toInt(),
          QStringLiteral("a higher-is-better knob moves the other way (%1 < %2)").arg(ws, wb));
}

TEST_SECTION("still and sequence destinations (image artifacts)", run_image_artifacts) {

    const QList<ctrl::ExportTarget> all =
        ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));

    // A still destination writes ONE image at the path the user names. Before the
    // artifact kind existed there was no way to express one at all: every row was a
    // video or an animated image, so "post an illustration" was not in the product.
    {
        const ctrl::ExportTarget still =
            ctrl::ExportTargets::find(all, QStringLiteral("still.pixiv.illustration"));
        check(still.artifact == ctrl::ArtifactKind::Still,
              "the pixiv illustration row is a still");

        ctrl::ExportRequest req;
        req.outputPath = aeTestTempPath("ae_still.png");
        req.sourceSize = QSize(1000, 800);
        req.fps = 30;
        req.selection = ctrl::FrameSelection::fromRange(0, 600); // the dialog's range
        req.playhead = 7;                                        // what is on screen
        ctrl::ExportPlan plan = ctrl::planExport(still, req, ctrl::QualityBucket::Balanced);
        check(plan.ok(), QStringLiteral("the still plans (%1)").arg(plan.error));
        check(plan.artifact == ctrl::ArtifactKind::Still, "the plan carries the artifact kind");
        check(plan.imageSuffix == QLatin1String("png"),
              QStringLiteral("the image format comes from the row (got '%1')").arg(plan.imageSuffix));
        check(plan.frames() == 1,
              QStringLiteral("a still is exactly one frame (got %1)").arg(plan.frames()));
        check(plan.selection.indices().value(0) == 7,
              QStringLiteral("the still is the PLAYHEAD frame, not the range start (got %1)")
                  .arg(plan.selection.indices().value(0)));
        check(!plan.audioEnabled, "a still carries no audio stream");
        check(plan.notes.join(QLatin1Char(' ')).contains(QLatin1String("One still image")),
              "the plan says in words that it is writing one image");

        // The suffix has to be one Qt can actually write, or the export produces
        // nothing and the failure surfaces as a missing file.
        QImage sample(64, 48, QImage::Format_RGBA8888);
        sample.fill(QColor(10, 20, 30, 128));
        const QString written = aeTestTempPath("ae_still_probe." + plan.imageSuffix);
        check(sample.save(written), QStringLiteral("Qt can write .%1").arg(plan.imageSuffix));
        const QImage back(written);
        check(!back.isNull() && back.size() == sample.size(),
              "the written still reads back at the planned size");
        QFile::remove(written);
    }

    // A frame SEQUENCE destination: pixiv's ugoira format is a folder of numbered
    // stills, not a video and not a GIF.
    {
        const ctrl::ExportTarget seq =
            ctrl::ExportTargets::find(all, QStringLiteral("post.pixiv.ugoira"));
        check(seq.artifact == ctrl::ArtifactKind::Sequence,
              "the ugoira row is a frame sequence");

        ctrl::ExportRequest req;
        req.outputPath = aeTestTempPath("ae_ugoira");
        req.sourceSize = QSize(800, 600);
        req.fps = 30;
        req.selection = ctrl::FrameSelection::fromRange(0, 399); // 400 frames
        ctrl::ExportPlan plan = ctrl::ExportPlan();
        plan = ctrl::planExport(seq, req, ctrl::QualityBucket::Balanced);
        check(plan.ok(), QStringLiteral("the sequence plans (%1)").arg(plan.error));
        check(plan.artifact == ctrl::ArtifactKind::Sequence, "the plan carries the sequence kind");
        // pixiv accepts at most 150 stills, so the plan has to trim rather than
        // hand the user a folder the site refuses.
        check(plan.frames() == 150,
              QStringLiteral("ugoira is capped at 150 frames, applied (got %1)").arg(plan.frames()));
        check(plan.imageSuffix == QLatin1String("png"), "ugoira frames are PNG here");
        check(seq.caps.maxBytes == 31457280, "the ugoira byte cap is the 30 MB total");
        check(plan.notes.join(QLatin1Char(' ')).contains(QLatin1String("numbered")),
              "the plan says the frames are numbered files");
    }

    // Every image row must name a format Qt can write, or the destination is a
    // promise the writer cannot keep. This is the cheap check that would have
    // caught a typo'd container in any of these rows.
    {
        int imageRows = 0;
        for (const ctrl::ExportTarget& t : all) {
            if (t.artifact == ctrl::ArtifactKind::Video) continue;
            ++imageRows;
            const QString suffix = t.container.toLower();
            QImage probe(8, 8, QImage::Format_RGBA8888);
            probe.fill(Qt::red);
            const QString path = aeTestTempPath("ae_suffix_probe." + suffix);
            const bool writable = probe.save(path);
            QFile::remove(path);
            check(writable, QStringLiteral("%1: .%2 is a writable image format")
                                .arg(t.id, suffix));
        }
        check(imageRows == 8,
              QStringLiteral("every image destination is covered here (found %1)").arg(imageRows));
    }
}

TEST_SECTION("post-encode fit-to-cap", run_fit_to_cap) {

    const QList<ctrl::ExportTarget> all =
        ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));
    const ctrl::ExportTarget base =
        ctrl::ExportTargets::find(all, QStringLiteral("telegram.sticker"));

    QList<QImage> frames;
    for (int i = 0; i < 6; ++i) frames.append(makeFrame(640, 480, i));

    ctrl::ExportRequest req;
    req.outputPath = aeTestTempPath("ae_fit.webm");
    req.sourceSize = QSize(640, 480);
    req.fps = 30;
    req.selection = ctrl::FrameSelection::fromCount(frames.size());

    // A cap small enough that the FIRST attempt cannot possibly fit, so the
    // loop has to degrade. File size is the one limit that cannot be known
    // before encoding — that is the whole reason this loop exists.
    ctrl::ExportTarget tiny = base;
    tiny.caps.maxBytes = 2500;

    const ctrl::FitResult r =
        ctrl::exportWithFit(tiny, req, ctrl::QualityBucket::Balanced, frames);
    for (const QString& step : r.steps) QTextStream(stdout) << "      " << step << "\n";
    check(r.attempts > 1, QStringLiteral("the fit loop iterated (%1 attempts)").arg(r.attempts));

    // Cancellation, polled before each attempt. Without this the loop ran to
    // completion (up to 16 encodes) while the progress dialog's Cancel button did
    // nothing — a button that lies about what it does.
    {
        int polls = 0;
        const ctrl::FitResult c = ctrl::exportWithFit(
            tiny, req, ctrl::QualityBucket::Balanced, frames, nullptr,
            [&polls] { return ++polls > 2; }); // let two attempts run, then stop
        check(c.canceled, "a canceled fit reports cancellation");
        check(!c.ok, "a canceled fit is not reported as success");
        check(c.attempts <= 3, QStringLiteral("the loop stopped promptly (%1 attempts)").arg(c.attempts));
        check(c.error == QLatin1String("canceled"),
              QStringLiteral("the reason is 'canceled', not the did-not-fit message (got '%1')").arg(c.error));
    }
    if (r.ok) {
        check(r.bytes <= tiny.caps.maxBytes,
              QStringLiteral("fitted under the cap: %1 <= %2 bytes")
                  .arg((qlonglong)r.bytes)
                  .arg((qlonglong)tiny.caps.maxBytes));
    } else {
        check(!r.error.isEmpty(),
              QStringLiteral("gave up cleanly with an explanation: %1").arg(r.error));
    }

    // An unreachable cap must be REPORTED, never silently accepted: a sticker
    // over its cap is a hard rejection at upload time.
    ctrl::ExportTarget impossible = base;
    impossible.caps.maxBytes = 100;
    const ctrl::FitResult r2 =
        ctrl::exportWithFit(impossible, req, ctrl::QualityBucket::Balanced, frames);
    check(!r2.ok && !r2.error.isEmpty(),
          "an impossible cap is reported, not silently accepted");
    QTextStream(stdout) << "      " << r2.error << "\n";
}

TEST_SECTION("a capped GIF destination is fitted, not just told about", run_gif_fit) {

    const QList<ctrl::ExportTarget> all =
        ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));
    const ctrl::ExportTarget base =
        ctrl::ExportTargets::find(all, QStringLiteral("wechat.sticker"));

    // The premise: a GIF destination that states a byte cap. WeChat rejects a
    // sticker above 500 KB, so the file has to be under it or the upload fails.
    check(!base.id.isEmpty() && base.caps.maxBytes > 0,
          QStringLiteral("the capped GIF row exists (%1, cap %2 bytes)")
              .arg(base.id)
              .arg((qlonglong)base.caps.maxBytes));

    const QSize source(640, 480);
    QList<QImage> frames;
    for (int i = 0; i < 8; ++i) frames.append(makeNoiseFrame(source.width(), source.height(), i));

    ctrl::ExportRequest req;
    req.outputPath = aeTestTempPath("ae_gif_fit.gif");
    req.sourceSize = source;
    req.fps = 15;
    req.selection = ctrl::FrameSelection::fromCount(frames.size());

    // GIF is media::GifEncoder's artifact, not VideoEncoder's. The fit loop used
    // to know only the video encoder, so this call could not produce a GIF at all
    // — and nothing noticed, because the app never reached the loop for a GIF
    // destination either: the GIF branch returned before it. A capped destination
    // whose cap is never applied hands the user a file the platform rejects, with
    // the limit printed in the dialog.
    // The row's own cap is not enough to force work here (WeChat's 240x240
    // sticker comes out at ~319 KB against a 500 KB cap, so the first attempt
    // fits). A cap that the first attempt cannot meet is what makes this test
    // about enforcement rather than about a happy first encode.
    ctrl::ExportTarget cappedGif = base;
    cappedGif.caps.maxBytes = 120000;
    const ctrl::FitResult r =
        ctrl::exportWithFit(cappedGif, req, ctrl::QualityBucket::Balanced, frames);
    for (const QString& step : r.steps) QTextStream(stdout) << "      " << step << "\n";
    check(r.ok, QStringLiteral("a capped GIF destination can be fitted (%1)").arg(r.error));
    check(r.attempts > 1,
          QStringLiteral("the GIF fit loop iterated, so the cap was actually applied (%1 attempts)")
              .arg(r.attempts));
    if (r.ok) {
        check(r.bytes <= cappedGif.caps.maxBytes,
              QStringLiteral("the GIF is inside the cap: %1 <= %2 bytes")
                  .arg((qlonglong)r.bytes)
                  .arg((qlonglong)cappedGif.caps.maxBytes));
        // Fitting must not change the ARTIFACT. A PNG under the cap would pass a
        // size check while being a file WeChat does not accept as a sticker.
        QFile f(r.path);
        bool headerOk = false;
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray head = f.read(6);
            headerOk = head.startsWith("GIF8");
        }
        check(headerOk, "the fitted file is still a GIF (Fitting changed the size, not the format)");
    }

    // The quality search must respect the table's [best, worst] ROLES, not their
    // numeric order. WebP's quality runs 80 (best) down to 30 (worst), and passing
    // 80 -> 30 into a bisection that assumes a rising range returned the worst value
    // on the first call: every capped WebP destination quantised as hard as allowed
    // even when a middling quality fitted. The tell is the number of probes.
    {
        const ctrl::ExportTarget webp =
            ctrl::ExportTargets::find(all, QStringLiteral("discord.emoji"));
        check(webp.codec == QLatin1String("libwebp"), "the Discord emoji row is WebP");
        const QSize src(320, 240);
        QList<QImage> noisy;
        for (int i = 0; i < 4; ++i)
            noisy.append(makeNoiseFrame(src.width(), src.height(), i));
        ctrl::ExportRequest wreq;
        wreq.outputPath = aeTestTempPath("ae_webp_quality.webp");
        wreq.sourceSize = src;
        wreq.fps = 12;
        wreq.selection = ctrl::FrameSelection::fromCount(noisy.size());
        ctrl::ExportTarget capped = webp;
        capped.degradeOrder = QStringList{QStringLiteral("quality")};
        // The initial attempt (quality 80, the row's best) comes to ~21 KB at this
        // size, so the cap has to be under that or the fit succeeds immediately and
        // there is no search to observe.
        capped.caps.maxBytes = 12000;
        const ctrl::FitResult w =
            ctrl::exportWithFit(capped, wreq, ctrl::QualityBucket::Balanced, noisy);
        for (const QString& step : w.steps) QTextStream(stdout) << "      " << step << "\n";
        int probes = 0;
        for (const QString& step : w.steps) {
            if (step.startsWith(QLatin1String("quality"))) ++probes;
        }
        check(probes > 1,
              QStringLiteral("the WebP fit SEARCHES its quality range (%1 probes; 1 means it "
                             "took the floor immediately)").arg(probes));
        if (w.ok) {
            check(w.bytes <= capped.caps.maxBytes,
                  QStringLiteral("the WebP fit is inside the cap (%1 <= %2)")
                      .arg((qlonglong)w.bytes)
                      .arg((qlonglong)capped.caps.maxBytes));
        }
    }

    // A GIF has no CRF, so before this it could only be fitted by shrinking the
    // image or dropping frames — both of which change what the animation IS. Its
    // palette size is the quality dimension, and it is declared in the same table
    // as every other codec's knob.
    {
        ctrl::ExportTarget gifTarget = base; // wechat.sticker: a GIF with a 500 KB cap
        gifTarget.degradeOrder = QStringList{QStringLiteral("quality")};
        const QSize small(320, 240);
        QList<QImage> noisy;
        for (int i = 0; i < 4; ++i)
            noisy.append(makeNoiseFrame(small.width(), small.height(), i));

        ctrl::ExportRequest greq;
        greq.outputPath = aeTestTempPath("ae_gif_palette.gif");
        greq.sourceSize = small;
        greq.fps = 12;
        greq.selection = ctrl::FrameSelection::fromCount(noisy.size());

        ctrl::ExportTarget capped = gifTarget;
        capped.caps.maxBytes = 60000; // unreachable at 256 colours, reachable lower
        const ctrl::FitResult g =
            ctrl::exportWithFit(capped, greq, ctrl::QualityBucket::Balanced, noisy);
        for (const QString& step : g.steps) QTextStream(stdout) << "      " << step << "\n";
        check(g.ok, QStringLiteral("a capped GIF can be fitted by palette size (%1)").arg(g.error));
        if (g.ok) {
            check(g.bytes <= capped.caps.maxBytes,
                  QStringLiteral("the colour-fitted GIF is inside the cap (%1 <= %2)")
                      .arg((qlonglong)g.bytes)
                      .arg((qlonglong)capped.caps.maxBytes));
            // The point of this dimension: the DIMENSIONS survive. Fitting by scale
            // would have changed them, which is why the quality phase must run first.
            // The size to compare against is the PLAN's (this row forces 240x240), not
            // the source's: a palette fit must not shrink it further.
            const ctrl::ExportPlan gplan = ctrl::planExport(capped, greq, ctrl::QualityBucket::Balanced);
            int w = 0, h = 0;
            const int n = decodeInfo(g.path, &w, &h);
            check(n > 0 && w == gplan.size.width() && h == gplan.size.height(),
                  QStringLiteral("the GIF kept its planned size while it got smaller "
                                 "(%1x%2, expected %3x%4, %5 frames)")
                      .arg(w)
                      .arg(h)
                      .arg(gplan.size.width())
                      .arg(gplan.size.height())
                      .arg(n));
        }
    }

    // The stride is a frame count. It was applied twice (once in the fit loop's
    // attempt builder and again inside the encoder call), so asking to keep every
    // 2nd frame wrote every 4th: fewer frames than the plan believed, and a log
    // line naming a stride the file did not have. Only the first attempt (stride
    // 1) was unaffected, which is why the existing fit test never saw it.
    {
        ctrl::ExportTarget framesOnly = base;
        framesOnly.degradeOrder = QStringList{QStringLiteral("frames")};
        const QSize small(160, 120);
        QList<QImage> noisy;
        for (int i = 0; i < 12; ++i)
            noisy.append(makeNoiseFrame(small.width(), small.height(), i));

        ctrl::ExportRequest freq;
        freq.outputPath = aeTestTempPath("ae_gif_stride.gif");
        freq.sourceSize = small;
        freq.fps = 10;
        freq.selection = ctrl::FrameSelection::fromCount(noisy.size());

        ctrl::ExportTarget tiny = framesOnly;
        // Reachable by dropping frames, unreachable without dropping any: one
        // noisy 240x180 frame is ~42 KB and all twelve are ~374 KB.
        tiny.caps.maxBytes = 120000;
        const ctrl::FitResult f2 =
            ctrl::exportWithFit(tiny, freq, ctrl::QualityBucket::Balanced, noisy);
        for (const QString& step : f2.steps) QTextStream(stdout) << "      " << step << "\n";
        check(f2.attempts > 1, QStringLiteral("the frames phase ran (%1 attempts)").arg(f2.attempts));
        if (f2.ok) {
            // The winning stride comes from the loop's own log; the file has to
            // agree with it. Frames are written at 0, s, 2s, ... so the count is
            // ceil(n/s), NOT n/s rounded any other way.
            int stride = 1;
            for (const QString& step : f2.steps) {
                const int at = step.indexOf(QLatin1String("stride="));
                if (at >= 0) {
                    const QString tail = step.mid(at + 7);
                    bool okNum = false;
                    const int v = tail.section(QLatin1Char(' '), 0, 0).toInt(&okNum);
                    if (okNum && v > 0) stride = v;
                }
            }
            const int expected = (noisy.size() + stride - 1) / stride;
            const int actual = gifFrameCount(f2.path);
            check(actual == expected,
                  QStringLiteral("the file has the frames the stride says: %1 written, %2 expected "
                                 "(stride %3 of %4)")
                      .arg(actual)
                      .arg(expected)
                      .arg(stride)
                      .arg(noisy.size()));
        } else {
            check(!f2.error.isEmpty(), QStringLiteral("the frames-only fit explained itself: %1").arg(f2.error));
        }
    }
}

// ---------------------------------------------------------------------------
// What a byte cap COSTS.
//
// A capped destination is not one encode: the loop encodes, measures, and
// re-encodes until the file fits, and every attempt is a full encode from the
// retained frames. The search used to bisect its whole quality range, which is
// ~7 probes wherever the cap falls, plus the initial attempt and the final
// re-encode of the winner: 9 encodes for a file the user asked for once.
//
// The cap is placed at HALF the codec's own best-quality size rather than at
// the row's published number, because the row's number depends on how
// compressible the fixture is (a 512x512 noise frame is 2 MB, a flat one is
// 20 KB, and Telegram's 256 KB cap binds on one and not the other). What is
// under test here is how many encodes the search spends crossing the cap, and
// that must not depend on the fixture.
//
// The ceiling is a gate, not a hope: it is the measured attempt count of the
// shipped search plus nothing. Tightening it is how a smarter search proves
// itself, and the numbers that set it are in the printed report.
// Measured on this fixture set, one run each, identical input:
//
//   row                  codec      blind bisection   interpolated search
//   telegram.sticker     libvpx-vp9        10                 5
//   chat.generic.video   libx264           10                 5
//   discord.emoji        libwebp           11                10
//   wechat.sticker       gif                9                 9
//   total                                  40                29
//
// The ceiling is one above the worst measured case (the WebP scale axis, where the
// byte curve is steepest near the cap so the crossing is hardest to localise), and the
// budget covers the set: a single case drifting back up to the old cost is caught even
// if another improves.
static const int kFitAttemptCeiling = 11;
static const int kFitAttemptBudget = 34; // four cases; measured 29, bisection 40

// What one attempt actually put in front of the encoder, read back out of the
// loop's own log. A line looks like
//
//   "scale: crf=29 165x165 stride=1 -> 51234 bytes"
//
// and a required-size row adds the size the ART was drawn at, because there the
// file keeps the size the destination demands:
//
//   "scale: crf=26 512x512 stride=1 -> 20735 bytes (art drawn at 128x128)"
//
// Two lines with the same setting are one encode paid for twice: encoding is
// deterministic, so the second measurement cannot differ from the first and
// cannot move the search. That is the whole cost this loop is judged on, so it is
// asserted rather than eyeballed. The `final` line re-encodes the winning setting
// on purpose (the search's last probe is not the setting it chose) and is not a
// probe, so it is not part of the invariant.
static int duplicateSettings(const QStringList& aSteps, QString* aWhat) {
    static const QRegularExpression re(QStringLiteral(
        "^(initial|quality|scale|frames): crf=(-?\\d+) (\\d+)x(\\d+) stride=(\\d+) ->"));
    static const QRegularExpression art(QStringLiteral("\\(art drawn at (\\d+)x(\\d+)\\)"));
    QSet<QString> seen;
    int dups = 0;
    for (const QString& step : aSteps) {
        const QRegularExpressionMatch m = re.match(step);
        if (!m.hasMatch()) continue; // the `final` re-encode, or a summary line
        const QRegularExpressionMatch am = art.match(step);
        const QString key = QStringLiteral("crf%1/%2x%3/stride%4/%5")
                                .arg(m.captured(2), m.captured(3), m.captured(4), m.captured(5),
                                     am.hasMatch() ? QStringLiteral("art%1x%2").arg(am.captured(1), am.captured(2))
                                                   : QStringLiteral("art-"));
        if (seen.contains(key)) {
            ++dups;
            if (aWhat) *aWhat = step;
        } else {
            seen.insert(key);
        }
    }
    return dups;
}

TEST_SECTION("a byte cap costs a bounded number of encodes", run_fit_cost) {
    const QList<ctrl::ExportTarget> all =
        ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));

    struct Case {
        const char* id;          // the row, so the codec and its quality knob are the real ones
        ctrl::QualityBucket bucket;
        QSize source;
        int frames;
        int fps;
    };
    const Case cases[] = {
        {"telegram.sticker", ctrl::QualityBucket::Balanced, QSize(512, 512), 6, 15}, // VP9, crf
        {"chat.generic.video", ctrl::QualityBucket::Balanced, QSize(320, 240), 6, 15}, // H.264, crf
        {"discord.emoji", ctrl::QualityBucket::Balanced, QSize(320, 320), 6, 12},     // WebP, quality
        {"wechat.sticker", ctrl::QualityBucket::Balanced, QSize(320, 240), 6, 12},    // GIF, palette
    };

    int totalEncodes = 0;
    for (const Case& c : cases) {
        const ctrl::ExportTarget row = ctrl::ExportTargets::find(all, QString::fromLatin1(c.id));
        if (row.id.isEmpty()) {
            check(false, QStringLiteral("%1: row exists").arg(QString::fromLatin1(c.id)));
            continue;
        }

        ctrl::ExportRequest req;
        req.outputPath = aeTestTempPath(QStringLiteral("ae_fit_cost_%1.%2")
                                                   .arg(QString::fromLatin1(c.id)
                                                            .replace(QLatin1Char('.'), QLatin1Char('_')),
                                                        row.container.isEmpty() ? QStringLiteral("out")
                                                                                : row.container));
        req.sourceSize = c.source;
        req.fps = c.fps;
        req.selection = ctrl::FrameSelection::fromCount(c.frames);

        const ctrl::ExportPlan plan = ctrl::planExport(row, req, c.bucket);
        if (!plan.ok()) {
            check(false, QStringLiteral("%1: plan builds (%2)").arg(QString::fromLatin1(c.id), plan.error));
            continue;
        }
        QList<QImage> frames;
        for (int i = 0; i < plan.frames(); ++i)
            frames.append(makeNoiseFrame(plan.renderSize.width(), plan.renderSize.height(), i));

        // 1. What the codec's own best quality produces, uncapped. One encode.
        ctrl::ExportTarget uncapped = row;
        uncapped.caps.maxBytes = 0; // 0 = no cap, one encode, no loop
        const ctrl::FitResult best = ctrl::exportWithFit(uncapped, req, c.bucket, frames);
        if (!best.ok) {
            check(false, QStringLiteral("%1: uncapped encode (%2)").arg(QString::fromLatin1(c.id), best.error));
            continue;
        }

        // 2. A cap the first attempt cannot meet, so the search has to work.
        ctrl::ExportTarget capped = row;
        capped.caps.maxBytes = std::max<qint64>(1024, best.bytes / 2);
        const ctrl::FitResult r = ctrl::exportWithFit(capped, req, c.bucket, frames);

        QTextStream(stdout) << "      " << c.id << " (" << plan.config.codec << "): best "
                            << (qlonglong)best.bytes << " bytes, cap " << (qlonglong)capped.caps.maxBytes
                            << " -> fitted " << (qlonglong)r.bytes << " in " << r.attempts
                            << " encodes\n";
        for (const QString& step : r.steps) QTextStream(stdout) << "        " << step << "\n";

        check(r.ok, QStringLiteral("%1: the capped fit succeeds (%2)").arg(QString::fromLatin1(c.id), r.error));
        if (!r.ok)
            continue;
        // Non-vacuity: if the first attempt already fitted, this measured nothing.
        check(r.attempts > 1,
              QStringLiteral("%1: the cap actually bound (%2 encodes)").arg(QString::fromLatin1(c.id)).arg(r.attempts));
        check(r.bytes <= capped.caps.maxBytes,
              QStringLiteral("%1: fitted inside the cap (%2 <= %3)")
                  .arg(QString::fromLatin1(c.id))
                  .arg((qlonglong)r.bytes)
                  .arg((qlonglong)capped.caps.maxBytes));
        totalEncodes += r.attempts;
        QString dupStep;
        const int dups = duplicateSettings(r.steps, &dupStep);
        check(dups == 0,
              QStringLiteral("%1: no setting is encoded twice (%2 repeat(s); last: %3)")
                  .arg(QString::fromLatin1(c.id))
                  .arg(dups)
                  .arg(dupStep));
        check(r.attempts <= kFitAttemptCeiling,
              QStringLiteral("%1: fitted in %2 encodes, ceiling %3 (a cap must not cost a second of "
                             "encoding per probe)")
                  .arg(QString::fromLatin1(c.id))
                  .arg(r.attempts)
                  .arg(kFitAttemptCeiling));
    }
    check(totalEncodes <= kFitAttemptBudget,
          QStringLiteral("four capped fits cost %1 encodes together, budget %2 (a cap is not "
                         "allowed to multiply the work the user asked for)")
              .arg(totalEncodes)
              .arg(kFitAttemptBudget));

    // ---------------------------------------------------------------------
    // A percent is not a pixel count.
    //
    // The scale phase searches a percent, but the encoder is handed a SIZE: the
    // attempt is drawn at evenFloor(size * pct/100), so on a small canvas several
    // percents are the same encode, and a search that indexes percents can spend
    // two probes on one setting. Counting the settings behind the 25%..100% floor
    // this table uses: a canvas 240 wide or larger offers 76 percents and 76
    // distinct settings (nothing wasted); 128x96 offers 61, and 96x64 offers 49 -
    // 27 percents that encode something already paid for.
    //
    // The check above caught it before these cases existed, in discord.emoji: that
    // row's PLAN is 128x128 however large the source is, and its scale phase probed
    // "crf=30 124x124" twice for 32842 bytes both times - one wasted encode, whose
    // second measurement could not have differed from the first. The PLAN size is
    // what matters, not the source size, which is why the case set above had to be
    // measured with the check in place rather than reasoned about. These cases cover
    // the small end (49 and 61 settings) so the domain stays the settings themselves
    // and not a proxy for them.
    {
        struct Small {
            const char* id;
            QSize source;
            const char* why;
            int capDivisor; // cap = best-quality size / this
        };
        const Small smalls[] = {
            {"chat.generic.video", QSize(96, 64), "49 settings behind 76 percents", 2},
            {"discord.emoji", QSize(128, 96), "61 settings behind 76 percents", 2},
            {"wechat.sticker", QSize(128, 96), "61 settings, exact size required", 2},
            // A REQUIRED size under a cap tight enough that the scale axis has to be
            // searched rather than resolved in one probe. This is the case that makes
            // the art size part of the duplicate key load-bearing: on these rows the
            // OUTPUT is always the required size, so two attempts at different art
            // sizes share their crf, their output size and their stride, and only the
            // art size tells them apart. Without it in the key the check would report
            // duplicates that are not duplicates. Verified by breaking the parse: this
            // case fails, the other three do not notice.
            {"telegram.emoji", QSize(512, 512), "required 100x100, tight cap", 8},
        };
        for (const Small& s : smalls) {
            const ctrl::ExportTarget row = ctrl::ExportTargets::find(all, QString::fromLatin1(s.id));
            if (row.id.isEmpty()) {
                check(false, QStringLiteral("%1: row exists").arg(QString::fromLatin1(s.id)));
                continue;
            }
            ctrl::ExportRequest req;
            req.outputPath = aeTestTempPath(QStringLiteral("ae_fit_small_%1.%2")
                                         .arg(QString::fromLatin1(s.id)
                                                  .replace(QLatin1Char('.'), QLatin1Char('_')),
                                              row.container));
            req.sourceSize = s.source;
            req.fps = 12;
            req.selection = ctrl::FrameSelection::fromCount(6);

            const ctrl::ExportPlan plan = ctrl::planExport(row, req, ctrl::QualityBucket::Balanced);
            if (!plan.ok()) {
                check(false, QStringLiteral("%1 @ %2x%3: plan builds (%4)")
                                 .arg(QString::fromLatin1(s.id))
                                 .arg(s.source.width())
                                 .arg(s.source.height())
                                 .arg(plan.error));
                continue;
            }
            QList<QImage> frames;
            for (int i = 0; i < plan.frames(); ++i)
                frames.append(makeNoiseFrame(plan.renderSize.width(), plan.renderSize.height(), i));

            ctrl::ExportTarget uncapped = row;
            uncapped.caps.maxBytes = 0;
            const ctrl::FitResult best = ctrl::exportWithFit(uncapped, req, ctrl::QualityBucket::Balanced, frames);
            if (!best.ok) {
                check(false, QStringLiteral("%1: uncapped encode (%2)")
                                 .arg(QString::fromLatin1(s.id), best.error));
                continue;
            }
            ctrl::ExportTarget capped = row;
            capped.caps.maxBytes = std::max<qint64>(512, best.bytes / std::max(2, s.capDivisor));
            const ctrl::FitResult r = ctrl::exportWithFit(capped, req, ctrl::QualityBucket::Balanced, frames);

            QTextStream(stdout) << "      " << s.id << " @ " << s.source.width() << "x" << s.source.height()
                                << " (" << s.why << "): best " << (qlonglong)best.bytes << " bytes, cap "
                                << (qlonglong)capped.caps.maxBytes << " -> fitted " << (qlonglong)r.bytes
                                << " in " << r.attempts << " encodes\n";
            for (const QString& step : r.steps) QTextStream(stdout) << "        " << step << "\n";

            check(r.ok, QStringLiteral("%1 @ %2x%3: the capped fit succeeds (%4)")
                            .arg(QString::fromLatin1(s.id))
                            .arg(s.source.width())
                            .arg(s.source.height())
                            .arg(r.error));
            if (!r.ok) continue;
            check(r.attempts > 1,
                  QStringLiteral("%1 @ %2x%3: the cap actually bound (%4 encodes)")
                      .arg(QString::fromLatin1(s.id))
                      .arg(s.source.width())
                      .arg(s.source.height())
                      .arg(r.attempts));
            check(r.bytes <= capped.caps.maxBytes,
                  QStringLiteral("%1 @ %2x%3: fitted inside the cap (%4 <= %5)")
                      .arg(QString::fromLatin1(s.id))
                      .arg(s.source.width())
                      .arg(s.source.height())
                      .arg((qlonglong)r.bytes)
                      .arg((qlonglong)capped.caps.maxBytes));
            QString dupStep;
            const int dups = duplicateSettings(r.steps, &dupStep);
            check(dups == 0,
                  QStringLiteral("%1 @ %2x%3: no setting is encoded twice (%4 repeat(s); last: %5)")
                      .arg(QString::fromLatin1(s.id))
                      .arg(s.source.width())
                      .arg(s.source.height())
                      .arg(dups)
                      .arg(dupStep));
        }
    }

    // ---------------------------------------------------------------------
    // A Required size is not a knob.
    //
    // Eight rows in the table demand an EXACT output size AND cap the bytes
    // (telegram.emoji 100x100/256 KB, discord.sticker 320x320/512 KB,
    // whatsapp.sticker and signal.sticker 512x512/500+300 KB, twitch.emote
    // 112x112/1 MB, wechat.sticker 240x240/500 KB, kakaotalk.sticker
    // 360x360/650 KB, qq.sticker 300x300/300 KB). Their size rule is the same
    // kind of hard requirement as the byte cap: Telegram rejects an emoji that is
    // not 100x100 exactly, Signal rejects a sticker that is not 512x512.
    //
    // So the fit loop may not buy bytes with the size on those rows. Scaling is
    // the one degrade phase that would: it produces a file that is small enough
    // and the wrong shape, which the platform rejects for the other reason. This
    // is the failure mode the scale phase was written for on Max rows, where
    // shrinking is legal, and it must not run on Exact ones.
    {
        const ctrl::ExportTarget row =
            ctrl::ExportTargets::find(all, QStringLiteral("signal.sticker"));
        check(!row.id.isEmpty() && row.size.mode == ctrl::SizeMode::Exact && row.size.w == 512
                  && row.size.h == 512,
              QStringLiteral("the row demands an exact 512x512 (%1, cap %2 bytes)")
                  .arg(row.id)
                  .arg((qlonglong)row.caps.maxBytes));
        check(row.degradeOrder.contains(QLatin1String("scale")),
              "the row does allow the scale phase, so the guard is what has to stop it");

        ctrl::ExportRequest ereq;
        ereq.outputPath = aeTestTempPath(QStringLiteral("ae_exact_size.apng"));
        ereq.sourceSize = QSize(512, 512);
        ereq.fps = 12;
        ereq.selection = ctrl::FrameSelection::fromCount(6);
        const ctrl::ExportPlan eplan = ctrl::planExport(row, ereq, ctrl::QualityBucket::Balanced);
        if (!eplan.ok()) {
            check(false, QStringLiteral("signal.sticker: plan builds (%1)").arg(eplan.error));
        } else {
            QList<QImage> eframes;
            for (int i = 0; i < eplan.frames(); ++i)
                eframes.append(makeNoiseFrame(eplan.renderSize.width(), eplan.renderSize.height(), i));

            ctrl::ExportTarget euncapped = row;
            euncapped.caps.maxBytes = 0;
            const ctrl::FitResult ebest = ctrl::exportWithFit(euncapped, ereq, ctrl::QualityBucket::Balanced, eframes);
            ctrl::ExportTarget ecapped = row;
            ecapped.caps.maxBytes = std::max<qint64>(1024, ebest.bytes / 3);
            const ctrl::FitResult e = ctrl::exportWithFit(ecapped, ereq, ctrl::QualityBucket::Balanced, eframes);
            for (const QString& step : e.steps) QTextStream(stdout) << "        " << step << "\n";
            QTextStream(stdout) << "      signal.sticker: best " << (qlonglong)ebest.bytes << " bytes, cap "
                                << (qlonglong)ecapped.caps.maxBytes << " -> " << (qlonglong)e.bytes
                                << " bytes at " << e.finalSize.width() << "x" << e.finalSize.height() << "\n";

            int ew = 0, eh = 0;
            const int en = e.ok ? decodeInfo(e.path, &ew, &eh) : 0;
            check(e.ok, QStringLiteral("signal.sticker: the capped fit succeeds (%1)").arg(e.error));
            if (e.ok) {
                check(ew == 512 && eh == 512,
                      QStringLiteral("the fitted sticker is still the exact size the destination "
                                     "requires (%1x%2 decoded, %3x%4 in the result; the cap is not "
                                     "allowed to buy bytes with the size)")
                          .arg(ew)
                          .arg(eh)
                          .arg(e.finalSize.width())
                          .arg(e.finalSize.height()));
                check(en > 0, QStringLiteral("the fitted sticker decodes (%1 frames)").arg(en));
                check(e.bytes <= ecapped.caps.maxBytes,
                      QStringLiteral("signal.sticker: fitted inside the cap (%1 <= %2)")
                          .arg((qlonglong)e.bytes)
                          .arg((qlonglong)ecapped.caps.maxBytes));
            }
        }
    }
}

TEST_SECTION("live encode from a resolved plan", run_live_encode) {

    const QList<ctrl::ExportTarget> all =
        ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));

    struct Case {
        const char* id;
        ctrl::QualityBucket bucket;
        QSize source;
        int frames;
        const char* ext;
        bool decode; // false when FFmpeg's decoder can't read it back (animated WebP)
    };

    const Case cases[] = {
        {"telegram.sticker", ctrl::QualityBucket::Balanced, QSize(1000, 800), 3, "webm", true},
        {"discord.sticker", ctrl::QualityBucket::Balanced, QSize(1000, 800), 3, "apng", true},
        {"obs.overlay", ctrl::QualityBucket::Balanced, QSize(320, 240), 3, "webm", true},
        {"youtube.video", ctrl::QualityBucket::Best, QSize(320, 240), 5, "mp4", true},
        {"archive.master", ctrl::QualityBucket::Lossless, QSize(320, 240), 3, "mkv", true},
    };

    for (const Case& c : cases) {
        const ctrl::ExportTarget t = ctrl::ExportTargets::find(all, QString::fromLatin1(c.id));
        if (t.id.isEmpty()) {
            check(false, QStringLiteral("%1: row exists").arg(QString::fromLatin1(c.id)));
            continue;
        }

        const QString out = aeTestTempPath(QStringLiteral("ae_target_%1.%2").arg(
            QString::fromLatin1(c.id).replace(QLatin1Char('.'), QLatin1Char('_')),
            QString::fromLatin1(c.ext)));

        ctrl::ExportRequest req;
        req.outputPath = out;
        req.sourceSize = c.source;
        req.fps = 30;
        req.selection = ctrl::FrameSelection::fromCount(c.frames);

        const ctrl::ExportPlan plan = ctrl::planExport(t, req, c.bucket);
        if (!plan.ok()) {
            check(false, QStringLiteral("%1: plan builds (%2)").arg(QString::fromLatin1(c.id), plan.error));
            continue;
        }

        QTextStream(stdout) << "      " << c.id << " -> " << plan.config.codec << " "
                            << plan.size.width() << "x" << plan.size.height() << " @ " << plan.fps
                            << "fps, alpha=" << (plan.alphaEnabled ? "yes" : "no") << "\n";
        for (const QString& n : plan.notes) QTextStream(stdout) << "        - " << n << "\n";

        // Render at the plan's RENDER size, exactly as the app would: the
        // encoder is what fits + pads to the output size.
        QList<QImage> frames;
        for (int i = 0; i < plan.frames(); ++i) {
            frames.append(makeFrame(plan.renderSize.width(), plan.renderSize.height(), i));
        }

        media::VideoEncoder enc;
        const bool wrote = enc.open(plan.config) && enc.writeAll(frames, plan.config);
        if (!wrote) {
            check(false, QStringLiteral("%1: encode succeeds (%2)").arg(QString::fromLatin1(c.id), enc.error()));
            continue;
        }
        check(QFileInfo(out).size() > 0, QStringLiteral("%1: file written").arg(QString::fromLatin1(c.id)));

        if (!c.decode) continue;
        int w = 0, h = 0;
        const int n = decodeInfo(out, &w, &h);
        check(n > 0, QStringLiteral("%1: decodes back (%2 frames)").arg(QString::fromLatin1(c.id)).arg(n));
        check(w == plan.size.width() && h == plan.size.height(),
              QStringLiteral("%1: decoded dims %2x%3 match the plan").arg(QString::fromLatin1(c.id)).arg(w).arg(h));
    }
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    // Every section that was DEFINED is in here. There is no list to keep in
    // sync, so a section cannot be written and silently not run — which is how
    // run_live_encode() went unexecuted while reading as coverage.
    for (const TestSection& s : testSections()) {
        QTextStream(stdout) << "\n== " << s.title << " ==\n";
        s.fn();
    }

    QTextStream(stdout) << "\n" << testSections().size() << " sections ran\n";
    QTextStream(stdout) << "\n" << (failures ? "FAILURES: " : "ALL PASS ") << failures << "\n";
    return failures ? 1 : 0;
}
