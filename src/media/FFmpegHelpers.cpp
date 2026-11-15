#include "media/FFmpegHelpers.h"

#include <QHash>
#include <QElapsedTimer>
#include <QDir>
#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
}

namespace media {
namespace ffmpeg {

QString license() {
    return QString::fromUtf8(avcodec_license());
}

QString configuration() {
    return QString::fromUtf8(avcodec_configuration());
}

bool encoderAvailable(const char* aEncoderName) {
    return ::avcodec_find_encoder_by_name(aEncoderName) != nullptr;
}

namespace {

QHash<QString, bool>& probeCache() {
    static QHash<QString, bool> cache;
    return cache;
}

// The one real capability test: open the encoder AND push a frame through it.
// Static capability queries are not trustworthy — avcodec_find_encoder_by_name
// reports encoders with no usable device, and avcodec_get_supported_config
// lists pixel formats the encoder then REFUSES at open time (libx265 advertises
// yuva420p and fails avcodec_open2 with ENOSYS). Only an actual open+encode
// answers the question.
//
// aPixFmt empty = let the encoder choose its first supported format.
bool probeImpl(const QString& aName, const QString& aPixFmt) {
    const AVCodec* codec = ::avcodec_find_encoder_by_name(aName.toUtf8().constData());
    if (!codec) return false; // not compiled into this libavcodec at all

    AVCodecContext* ctx = ::avcodec_alloc_context3(codec);
    if (!ctx) return false;

    ctx->width = 64;
    ctx->height = 64;
    ctx->time_base = AVRational{1, 25};
    ctx->framerate = AVRational{25, 1};

    if (!aPixFmt.isEmpty()) {
        ctx->pix_fmt = ::av_get_pix_fmt(aPixFmt.toUtf8().constData());
        if (ctx->pix_fmt == AV_PIX_FMT_NONE) {
            ::avcodec_free_context(&ctx);
            return false;
        }
    } else {
        // Ask the encoder which pixel formats it claims to support rather than
        // assuming yuv420p (FFmpeg 8 removed AVCodec::pix_fmts).
        ctx->pix_fmt = AV_PIX_FMT_NONE;
        const AVPixelFormat* fmts = nullptr;
        if (::avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0,
                                          (const void**)&fmts, nullptr) == 0 && fmts) {
            ctx->pix_fmt = fmts[0];
        }
        if (ctx->pix_fmt == AV_PIX_FMT_NONE) ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    }

    if (::avcodec_open2(ctx, codec, nullptr) < 0) {
        ::avcodec_free_context(&ctx);
        return false; // e.g. a hardware encoder with no capable device, or a
                      // pixel format the encoder lists but cannot encode
    }

    bool ok = false;
    AVFrame* frame = ::av_frame_alloc();
    if (frame) {
        frame->format = ctx->pix_fmt;
        frame->width = ctx->width;
        frame->height = ctx->height;
        if (::av_frame_get_buffer(frame, 32) >= 0) {
            // Zero it: encoders read every byte, and uninitialised memory would
            // both trip sanitizers and make the probe nondeterministic. Plane
            // heights are NOT all the frame height — chroma planes are
            // subsampled, so sizing them by frame->height overflows the buffer
            // (this corrupted the heap on yuv420p).
            const AVPixFmtDescriptor* desc =
                ::av_pix_fmt_desc_get((AVPixelFormat)frame->format);
            for (int p = 0; p < 4 && frame->data[p]; ++p) {
                int planeH = frame->height;
                if (desc && (p == 1 || p == 2)) {
                    planeH = AV_CEIL_RSHIFT(frame->height, desc->log2_chroma_h);
                }
                memset(frame->data[p], 0, (size_t)frame->linesize[p] * (size_t)planeH);
            }
            if (::avcodec_send_frame(ctx, frame) >= 0) {
                AVPacket* pkt = ::av_packet_alloc();
                if (pkt) {
                    const int r = ::avcodec_receive_packet(ctx, pkt);
                    // 0 = produced a packet. EAGAIN = it simply wants more frames
                    // before emitting one (normal for B-frame/lag encoders).
                    ok = (r >= 0 || r == AVERROR(EAGAIN));
                    ::av_packet_free(&pkt);
                }
            }
        }
        ::av_frame_free(&frame);
    }

    ::avcodec_free_context(&ctx);
    return ok;
}

bool cachedProbe(const QString& aName, const QString& aPixFmt) {
    const QString key = aName + QLatin1Char('|') + aPixFmt;
    auto& cache = probeCache();
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) return *it;
    const bool result = probeImpl(aName, aPixFmt);
    cache.insert(key, result);
    return result;
}

} // namespace

bool encoderUsable(const char* aEncoderName) {
    return cachedProbe(QString::fromUtf8(aEncoderName), QString());
}

// True iff the encoder can actually be opened AND encode a frame USING this
// pixel format. encoderSupportsPixFormat() only queries the encoder's claimed
// list; libx265 is a live counter-example (lists yuva420p, refuses at open).
bool encoderSupportsPixFormatUsable(const QString& aEncoderName, const QString& aPixFmt) {
    if (aPixFmt.isEmpty()) return encoderUsable(aEncoderName.toUtf8().constData());
    return cachedProbe(aEncoderName, aPixFmt);
}

QStringList availableVideoEncoders() {
    QStringList names;
    const AVCodec* codec = nullptr;
    void* opaque = nullptr;
    while ((codec = ::av_codec_iterate(&opaque)) != nullptr) {
        if (codec->type == AVMEDIA_TYPE_VIDEO && ::av_codec_is_encoder(codec)) {
            if (codec->name != nullptr) {
                names.append(QString::fromUtf8(codec->name));
            }
        }
    }
    // Deterministic, human-usable ordering (av_codec_iterate order is by
    // registration, not by name).
    std::sort(names.begin(), names.end());
    return names;
}

QString resolveEncoderName(const QString& aName) {
    // Friendly names (from the export-preset and the dialog's per-container
    // encoder combos) that differ from the libav encoder name. Names that are
    // already libav encoder names pass through unchanged.
    static const QHash<QString, QString> aliases = {
        { QStringLiteral("theora"), QStringLiteral("libtheora") },
        { QStringLiteral("x264"),    QStringLiteral("libx264") },
        { QStringLiteral("x265"),    QStringLiteral("libx265") },
        { QStringLiteral("h264"),    QStringLiteral("libx264") },
        { QStringLiteral("vp8"),     QStringLiteral("libvpx") },
        { QStringLiteral("vp9"),     QStringLiteral("libvpx-vp9") },
        { QStringLiteral("av1"),     QStringLiteral("libaom-av1") },
        { QStringLiteral("mpeg2"),   QStringLiteral("mpeg2video") },
    };
    const QString candidate = aliases.value(aName, aName);
    return encoderAvailable(candidate.toUtf8().constData()) ? candidate : QString();
}

// True iff the given container's muxer (e.g. "mp4", "webm", "mkv") can carry the
// named encoder's codec. Uses avformat_query_codec against the allocated muxer,
// so a requested codec that a container can't hold is REJECTED up front instead
// of surfacing as a confusing avformat_write_header failure.
bool muxerSupportsCodec(const QString& aContainer, const QString& aEncoderName) {
    const AVCodec* codec = ::avcodec_find_encoder_by_name(aEncoderName.toUtf8().constData());
    if (!codec)
        return false;
    AVFormatContext* oc = nullptr;
    const QByteArray fname = QDir::temp().filePath("ae_codec_probe.tmp").toUtf8();
    // "mkv" is not a registered muxer name (ffmpeg uses "matroska").
    const QByteArray container =
        (aContainer == QLatin1String("mkv")) ? QByteArrayLiteral("matroska")
                                             : aContainer.toUtf8();
    if (::avformat_alloc_output_context2(&oc, nullptr, container.constData(),
                                         fname.constData()) < 0) {
        return false;
    }
    const AVOutputFormat* ofmt = oc ? oc->oformat : nullptr;
    const int r = ofmt ? ::avformat_query_codec(ofmt, codec->id, FF_COMPLIANCE_NORMAL) : 0;
    ::avformat_free_context(oc);
    return r > 0;
}

// Pick a default video encoder the given container's muxer can actually carry
// (and that the linked libav* build provides), in priority order. Returns an
// empty string if none of the candidates fit, so the caller can reject cleanly
// instead of muxing a codec the container can't hold.
QString defaultCodecForContainer(const QString& aContainer) {
    static const char* kCandidates[] = {
        "libx264",       "libx265",   "libvpx-vp9", "libvpx",
        "mpeg4",         "libtheora", "ffv1",       "huffyuv",
        "prores_ks",     "utvideo",   "apng",       "libwebp",
    };
    const QByteArray container = aContainer.toUtf8();
    for (const char* name : kCandidates) {
        if (!encoderAvailable(name))
            continue;
        if (muxerSupportsCodec(aContainer, QString::fromUtf8(name)))
            return QString::fromUtf8(name);
    }
    return QString();
}

bool encoderSupportsPixFormat(const QString& aEncoderName, const QString& aPixFmt) {
    static auto pixfmtFromName = [](const QString& n) -> AVPixelFormat {
        if (n == QLatin1String("yuv420p"))  return AV_PIX_FMT_YUV420P;
        if (n == QLatin1String("yuv422p"))  return AV_PIX_FMT_YUV422P;
        if (n == QLatin1String("yuv444p"))  return AV_PIX_FMT_YUV444P;
        if (n == QLatin1String("yuva420p")) return AV_PIX_FMT_YUVA420P;
        if (n == QLatin1String("rgba"))     return AV_PIX_FMT_RGBA;
        if (n == QLatin1String("bgra"))     return AV_PIX_FMT_BGRA;
        if (n == QLatin1String("rgb24"))    return AV_PIX_FMT_RGB24;
        if (n == QLatin1String("gray"))     return AV_PIX_FMT_GRAY8;
        if (n == QLatin1String("yuv444p10le")) return AV_PIX_FMT_YUV444P10LE;
        return AV_PIX_FMT_NONE;
    };
    const AVPixelFormat fmt = pixfmtFromName(aPixFmt);
    if (fmt == AV_PIX_FMT_NONE) {
        // Unknown name: treat as unsupported so a typo is caught, not silently
        // passed to an encoder that may reject it at open() time.
        return false;
    }
    const AVCodec* codec = ::avcodec_find_encoder_by_name(aEncoderName.toUtf8().constData());
    if (!codec)
        return false;
    const void* out = nullptr;
    int n = 0;
    if (::avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &out, &n) < 0)
        return true; // unknown config -> assume any
    const enum AVPixelFormat* list = static_cast<const enum AVPixelFormat*>(out);
    if (!list)
        return true;
    for (int i = 0; list[i] != AV_PIX_FMT_NONE; ++i) {
        if (list[i] == fmt)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// which artifacts this build can actually produce
// ---------------------------------------------------------------------------

namespace {

struct KnownContainer {
    const char* name;
    const char* label;
};

// The containers worth offering, most wanted first. This table is the ONLY
// authored part: names and labels do not change, whereas which of them works
// does — and that is probed, never listed.
const KnownContainer kKnownContainers[] = {
    {"mp4",  "MP4"},
    {"webm", "WebM"},
    {"gif",  "GIF animation"},
    {"apng", "APNG animation"},
    {"webp", "WebP animation"},
    {"mov",  "QuickTime (MOV)"},
    {"mkv",  "Matroska (MKV)"},
    {"avi",  "AVI"},
    {"ogv",  "Ogg"},
};

struct KnownCodec {
    const char* encoder;
    const char* label;
    bool lossless;
};

// Friendly names for the encoders an animation tool plausibly wants, in the
// order a container should prefer them: COMPATIBILITY FIRST.
//
// This order is not a quality ranking, and getting it wrong is a real
// regression: putting AV1 ahead of VP9 made WebM default to AV1, which would
// have broken the single most common use of WebM here — a transparent overlay
// that OBS and browsers must decode. The first usable entry becomes a
// container's default, so the first entry has to be the one that plays
// everywhere, and the fancier codecs come after it.
const KnownCodec kKnownCodecs[] = {
    {"libx264",       "H.264",                  false},
    {"libx265",       "H.265",                  false},
    {"libvpx-vp9",    "VP9",                    false},
    {"libvpx",        "VP8",                    false},
    {"mpeg4",         "MPEG-4",                 false},
    {"libsvtav1",     "AV1",                    false},
    {"libaom-av1",    "AV1",                    false},
    {"h264_nvenc",    "H.264 (GPU)",            false},
    {"hevc_nvenc",    "H.265 (GPU)",            false},
    {"av1_nvenc",     "AV1 (GPU)",              false},
    {"h264_qsv",      "H.264 (Quick Sync)",     false},
    {"hevc_qsv",      "H.265 (Quick Sync)",     false},
    {"h264_vaapi",    "H.264 (VAAPI)",          false},
    {"hevc_vaapi",    "H.265 (VAAPI)",          false},
    {"prores_ks",     "ProRes 4444",            false},
    {"ffv1",          "FFV1 (lossless)",        true},
    {"utvideo",       "Ut Video (lossless)",    true},
    {"huffyuv",       "Huffyuv (lossless)",     true},
    {"libtheora",     "Theora",                 false},
    {"apng",          "APNG",                   false},
    {"libwebp",       "WebP",                   false},
};

const KnownCodec* knownCodec(const QString& aEncoder) {
    for (const KnownCodec& c : kKnownCodecs) {
        if (aEncoder == QLatin1String(c.encoder))
            return &c;
    }
    return nullptr;
}

// Can this encoder carry an alpha channel HERE? Asked of the encoder rather than
// read off the row, because the two answers differ in exactly the cases that
// matter: libx265 advertises yuva420p and then refuses to open with it.
bool alphaWorks(const QString& aEncoder) {
    return encoderSupportsPixFormatUsable(aEncoder, QStringLiteral("yuva420p"))
        || encoderSupportsPixFormatUsable(aEncoder, QStringLiteral("rgba"));
}

} // namespace

bool containerCarriesAudio(const QString& aContainer) {
    // Not a guess about the format: these are the containers the exporter has an
    // audio encoder for and that carry one in practice. WebM is the reason the
    // question matters at all — it cannot hold AAC, so its audio codec is derived
    // separately (libopus).
    static const QStringList kCarriers = {
        QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("webm"),
        QStringLiteral("mkv"), QStringLiteral("matroska"), QStringLiteral("avi"),
    };
    return kCarriers.contains(aContainer);
}

bool containerAcceptsUsableCodec(const QString& aContainer, const QString& aEncoderName) {
    if (aEncoderName.isEmpty())
        return false;
    if (!encoderUsable(aEncoderName.toUtf8().constData()))
        return false;
    return muxerSupportsCodec(aContainer, aEncoderName);
}

QList<ContainerOption> writableContainers() {
    // Probed once: each entry costs a one-frame encode, and the answer cannot
    // change while the process lives.
    static const QList<ContainerOption> kCache = [] {
        QList<ContainerOption> out;
        for (const KnownContainer& c : kKnownContainers) {
            const QString name = QString::fromUtf8(c.name);

            ContainerOption option;
            option.name = name;
            option.label = QString::fromUtf8(c.label);

            if (name == QLatin1String("gif")) {
                // GIF is the one artifact media::GifEncoder owns rather than
                // libav: it builds a palette per clip, which libav's gif encoder
                // does not. So it is present whenever the app is, and its codec
                // is named "gif" to route it there.
                option.defaultCodec = QStringLiteral("gif");
                option.alphaCapable = true;
                out.append(option);
                continue;
            }

            // The default must be one that RUNS here, not merely one that
            // compiled in: defaultCodecForContainer() only checks availability,
            // so walk its candidates until one actually opens.
            const QList<CodecOption> usable = usableCodecsForContainer(name);
            if (usable.isEmpty())
                continue;
            option.defaultCodec = usable.first().encoder;
            option.alphaCapable = usable.first().alphaCapable;
            out.append(option);
        }
        return out;
    }();
    return kCache;
}

QList<CodecOption> usableCodecsForContainer(const QString& aContainer) {
    QList<CodecOption> out;
    if (aContainer.isEmpty())
        return out;

    // Offerable = known name, runs here, and this muxer accepts it, in the
    // known list's order (compatibility first, lossless last).
    for (const KnownCodec& c : kKnownCodecs) {
        const QString encoder = QString::fromUtf8(c.encoder);
        if (!containerAcceptsUsableCodec(aContainer, encoder))
            continue;
        const KnownCodec* known = knownCodec(encoder);
        CodecOption option;
        option.encoder = encoder;
        option.label = QString::fromUtf8(known ? known->label : c.encoder);
        option.lossless = c.lossless;
        option.alphaCapable = alphaWorks(encoder);
        out.append(option);
    }
    return out;
}

} // namespace ffmpeg
} // namespace media
