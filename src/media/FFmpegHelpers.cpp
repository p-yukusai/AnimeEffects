#include "media/FFmpegHelpers.h"

#include <QHash>
#include <QDir>
#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>
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

} // namespace ffmpeg
} // namespace media
