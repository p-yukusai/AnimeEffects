#include "media/VideoEncoder.h"
#include "media/FFmpegHelpers.h"

#include <QList>
#include <QVector>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/dict.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace media {

QImage fitImage(const QImage& aImage, const QSize& aTarget, FitMode aFit) {
    if (aFit == FitMode::None || aImage.size() == aTarget || aTarget.isEmpty()) {
        return aImage;
    }
    const QImage src = (aImage.format() == QImage::Format_RGBA8888)
                           ? aImage
                           : aImage.convertToFormat(QImage::Format_RGBA8888);
    const Qt::AspectRatioMode mode = (aFit == FitMode::Pad)
                                         ? Qt::KeepAspectRatio
                                         : Qt::KeepAspectRatioByExpanding;
    const QImage fitted = src.scaled(aTarget, mode, Qt::SmoothTransformation);
    QImage canvas(aTarget, QImage::Format_RGBA8888);
    canvas.fill(0x00000000); // fully transparent; stickers want no background
    // KeepAspectRatioByExpanding can exceed the frame, so the offset may be
    // negative — that is the crop. Copy only the intersecting rectangle.
    const int ox = (aTarget.width() - fitted.width()) / 2;
    const int oy = (aTarget.height() - fitted.height()) / 2;
    const int sx = std::max(0, -ox);
    const int sy = std::max(0, -oy);
    const int cw = std::min(fitted.width() - sx, aTarget.width() - std::max(0, ox));
    const int ch = std::min(fitted.height() - sy, aTarget.height() - std::max(0, oy));
    for (int y = 0; y < ch; ++y) {
        std::memcpy(canvas.scanLine(std::max(0, oy) + y) + (size_t)std::max(0, ox) * 4,
                    fitted.constScanLine(sy + y) + (size_t)sx * 4, (size_t)cw * 4);
    }
    return canvas;
}


namespace {

AVPixelFormat pixfmtFromString(const QString& aName) {
    if (aName == QLatin1String("yuv420p"))  return AV_PIX_FMT_YUV420P;
    if (aName == QLatin1String("yuv422p"))  return AV_PIX_FMT_YUV422P;
    if (aName == QLatin1String("yuv444p"))  return AV_PIX_FMT_YUV444P;
    if (aName == QLatin1String("yuva420p")) return AV_PIX_FMT_YUVA420P;
    if (aName == QLatin1String("rgba"))     return AV_PIX_FMT_RGBA;
    if (aName == QLatin1String("bgra"))     return AV_PIX_FMT_BGRA;
    if (aName == QLatin1String("rgb24"))    return AV_PIX_FMT_RGB24;
    if (aName == QLatin1String("gray"))     return AV_PIX_FMT_GRAY8;
    return AV_PIX_FMT_NONE;
}

bool isYuvPlanar(AVPixelFormat aFmt) {
    switch (aFmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVA420P:
        return true;
    default:
        return false;
    }
}

// Modern (FFmpeg 8) replacement for the deprecated AVCodec::pix_fmts. Returns
// the codec's supported pixel formats (terminated by AV_PIX_FMT_NONE), or NULL
// if all formats are supported. The returned pointer is owned by the codec —
// it must NOT be freed by the caller (verified: it equals the codec's own
// static list).
const enum AVPixelFormat* codecPixFmtList(const AVCodec* aCodec) {
    const void* out = nullptr;
    int n = 0;
    if (::avcodec_get_supported_config(nullptr, aCodec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &out, &n) < 0)
        return nullptr;
    return static_cast<const enum AVPixelFormat*>(out);
}

bool codecSupportsPixFmt(const AVCodec* aCodec, AVPixelFormat aFmt) {
    const enum AVPixelFormat* list = codecPixFmtList(aCodec);
    if (!list)
        return true;
    for (int i = 0; list[i] != AV_PIX_FMT_NONE; ++i) {
        if (list[i] == aFmt)
            return true;
    }
    return false;
}

// Modern (FFmpeg 8) way to ask a codec for its first supported audio sample
// format (replaces the deprecated AVCodec::sample_fmts).
AVSampleFormat codecSampleFmt(const AVCodec* aCodec) {
    const void* out = nullptr;
    int n = 0;
    if (::avcodec_get_supported_config(nullptr, aCodec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &out, &n) < 0)
        return AV_SAMPLE_FMT_FLT;
    const enum AVSampleFormat* list = static_cast<const enum AVSampleFormat*>(out);
    if (!list)
        return AV_SAMPLE_FMT_FLT;
    return list[0];
}

} // namespace

// pimpl: keep libav* types out of the public header.
struct VideoEncoder::Impl {
    AVFormatContext* mFmt = nullptr;
    AVCodecContext* mCtx = nullptr;
    AVStream* mStream = nullptr;
    SwsContext* mSws = nullptr;
    AVFrame* mFrame = nullptr;
    AVPixelFormat mPixFmt = AV_PIX_FMT_NONE;
    bool mClosed = true;
    FitMode mFit = FitMode::None;

    // Optional audio stream (interleaved float, set via setAudio()).
    AVCodecContext* mAudioCtx = nullptr;
    AVStream* mAudioStream = nullptr;
    SwrContext* mAudioSwr = nullptr; // interleaved FLT -> codec sample_fmt
    AVSampleFormat mAudioFmt = AV_SAMPLE_FMT_NONE;
    QVector<float> mAudio; // interleaved
    int mAudioChannels = 0;
    int mAudioRate = 0;
    qint64 mAudioCount = 0; // per channel
    QString mAudioCodec;
};

VideoEncoder::VideoEncoder() : mImpl(new Impl()) {}
VideoEncoder::~VideoEncoder() { close(); delete mImpl; }

QSize VideoEncoder::normalizedSize(const QSize& aSize, const QString& aPixFmt) {
    const AVPixelFormat fmt = pixfmtFromString(aPixFmt);
    QSize s = aSize;
    if (fmt != AV_PIX_FMT_NONE && isYuvPlanar(fmt)) {
        if (s.width() % 2 != 0) s.setWidth(s.width() + 1);
        if (s.height() % 2 != 0) s.setHeight(s.height() + 1);
    }
    return s;
}

VideoEncoder::Config VideoEncoder::makeConfig(const QString& aContainer, const QString& aCodec,
                                              const QString& aPixFmt, bool aTransparent,
                                              int aWidth, int aHeight, int aFps, int aBitrateKbps,
                                              QString* aError) {
    Config c;
    c.container = (aContainer == QLatin1String("mkv")) ? QStringLiteral("matroska") : aContainer;
    c.width = aWidth;
    c.height = aHeight;
    c.fps = aFps;
    c.bitrate = aBitrateKbps > 0 ? aBitrateKbps * 1000 : 0; // preset bitrate is kbps
    c.transparent = aTransparent;

    const QString enc = ::media::ffmpeg::resolveEncoderName(aCodec);
    if (enc.isEmpty()) {
        if (aError) *aError = "encoder not available in this build: " + aCodec;
        return c; // codec stays empty -> callers reject
    }
    c.codec = enc;

    // Pixel format: honor the preset's explicit list if the encoder supports it,
    // otherwise prefer an alpha-capable format when transparency is requested.
    if (!aPixFmt.isEmpty() && ::media::ffmpeg::encoderSupportsPixFormat(enc, aPixFmt)) {
        c.pixFmt = aPixFmt;
    } else if (aTransparent && ::media::ffmpeg::encoderSupportsPixFormat(enc, QStringLiteral("rgba"))) {
        c.pixFmt = QStringLiteral("rgba");
    } else if (aTransparent && ::media::ffmpeg::encoderSupportsPixFormat(enc, QStringLiteral("yuva420p"))) {
        c.pixFmt = QStringLiteral("yuva420p");
    }
    return c;
}

bool VideoEncoder::open(const Config& aConfig) {
    // Closing any prior session first.
    close();

    mError.clear();
    mWrittenFrames = 0;

    if (aConfig.fps <= 0) { mError = "invalid fps (must be > 0)"; return false; }
    if (aConfig.width <= 0 || aConfig.height <= 0) { mError = "invalid dimensions"; return false; }
    if (aConfig.codec.isEmpty()) { mError = "no codec selected"; return false; }
    if (aConfig.outputPath.isEmpty()) { mError = "no output path"; return false; }

    // After this point the encoder owns libav* resources; on any failure it must
    // release them (otherwise a failed open leaks the half-constructed contexts —
    // the destructor can't tell a never-opened encoder from a failed one).
    auto fail = [&](const QString& aMsg) { mError = aMsg; cleanup(); return false; };

    // Determine target pixel format.
    const AVCodec* codec = ::avcodec_find_encoder_by_name(aConfig.codec.toUtf8().constData());
    if (!codec) { return fail("encoder not found: " + aConfig.codec); }

    AVPixelFormat pixFmt = AV_PIX_FMT_NONE;
    if (!aConfig.pixFmt.isEmpty()) {
        pixFmt = pixfmtFromString(aConfig.pixFmt);
        if (pixFmt == AV_PIX_FMT_NONE || !codecSupportsPixFmt(codec, pixFmt)) {
            pixFmt = AV_PIX_FMT_NONE; // fall through to auto
        }
    }
    if (pixFmt == AV_PIX_FMT_NONE) {
        if (aConfig.transparent) {
            if (codecSupportsPixFmt(codec, AV_PIX_FMT_RGBA))
                pixFmt = AV_PIX_FMT_RGBA;
            else if (codecSupportsPixFmt(codec, AV_PIX_FMT_YUVA420P))
                pixFmt = AV_PIX_FMT_YUVA420P;
        }
        if (pixFmt == AV_PIX_FMT_NONE && codecPixFmtList(codec))
            pixFmt = codecPixFmtList(codec)[0];
        if (pixFmt == AV_PIX_FMT_NONE)
            pixFmt = AV_PIX_FMT_YUV420P;
    }

    const QSize size = normalizedSize(QSize(aConfig.width, aConfig.height),
                                      QString::fromUtf8(av_get_pix_fmt_name(pixFmt)));
    const int width = size.width();
    const int height = size.height();

    // Muxer. The container name may be an alias that isn't a registered muxer
    // (e.g. "mkv" vs "matroska"); if the named muxer fails, fall back to
    // guessing the muxer from the output file extension. Bind the UTF-8 buffer
    // to a local so the `container` pointer is not dangling (a temporary
    // QByteArray from toUtf8() would be destroyed at the end of this statement).
    const QByteArray containerBytes = aConfig.container.toUtf8();
    const char* container = aConfig.container.isEmpty() ? nullptr : containerBytes.constData();
    if (::avformat_alloc_output_context2(&mImpl->mFmt, nullptr, container,
                                         aConfig.outputPath.toUtf8().constData()) < 0) {
        mImpl->mFmt = nullptr;
        if (::avformat_alloc_output_context2(&mImpl->mFmt, nullptr, nullptr,
                                             aConfig.outputPath.toUtf8().constData()) < 0) {
            return fail("could not allocate muxer for " + aConfig.outputPath);
        }
    }

    // Codec context.
    mImpl->mCtx = ::avcodec_alloc_context3(codec);
    if (!mImpl->mCtx) { return fail("could not allocate codec context"); }

    // Muxers that declare AVFMT_GLOBALHEADER (matroska, mp4, mov, ...) need the
    // codec's extradata up front in the container header. Without this flag the
    // encoder is never asked for it and avformat_write_header fails with
    // AVERROR_INVALIDDATA. Some codec/container pairs happen to cope, which is
    // why this went unnoticed until a matroska+libx265 encode was exercised.
    if (mImpl->mFmt->oformat->flags & AVFMT_GLOBALHEADER) {
        mImpl->mCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    mImpl->mCtx->width = width;
    mImpl->mCtx->height = height;
    mImpl->mCtx->pix_fmt = pixFmt;
    mImpl->mCtx->time_base = AVRational{ 1, aConfig.fps };
    mImpl->mCtx->framerate = AVRational{ aConfig.fps, 1 };
    mImpl->mCtx->gop_size = 12;
    if (aConfig.bitrate > 0)
        mImpl->mCtx->bit_rate = aConfig.bitrate;
    if (aConfig.quality >= 0 &&
        (codec->id == AV_CODEC_ID_H264 || codec->id == AV_CODEC_ID_H265)) {
        // Map a 0-100 quality shorthand onto -crf for x264/x265.
        // (Ignored for other codecs; they keep their own default.)
        const int crf = 51 - (aConfig.quality * 51) / 100;
        ::av_opt_set_int(mImpl->mCtx->priv_data, "crf", crf, 0);
    }

    // Private encoder options (e.g. libvpx "auto-alt-ref"). avcodec_open2
    // consumes the entries it recognises and LEAVES the rest, which is how we
    // detect a typo instead of silently producing wrong output.
    AVDictionary* opts = nullptr;
    for (auto it = aConfig.encOptions.constBegin(); it != aConfig.encOptions.constEnd(); ++it) {
        const QByteArray k = it.key().toUtf8();
        const QByteArray v = it.value().toUtf8();
        ::av_dict_set(&opts, k.constData(), v.constData(), 0);
    }
    const int openErr = ::avcodec_open2(mImpl->mCtx, codec, &opts);
    if (opts) {
        AVDictionaryEntry* leftover = ::av_dict_get(opts, "", nullptr, AV_DICT_IGNORE_SUFFIX);
        const QString unknown = leftover ? QString::fromUtf8(leftover->key) : QString();
        ::av_dict_free(&opts);
        if (leftover) {
            return fail(QStringLiteral("unknown encoder option '%1' for %2")
                            .arg(unknown, aConfig.codec));
        }
    }
    if (openErr < 0) {
        // Report libav's own reason: "could not open encoder" alone is useless
        // when the real cause is an unsupported pixel format or a missing
        // device.
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        ::av_strerror(openErr, errbuf, sizeof(errbuf));
        return fail(QStringLiteral("could not open encoder %1: %2")
                        .arg(aConfig.codec, QString::fromUtf8(errbuf)));
    }
    mImpl->mPixFmt = pixFmt;
    mImpl->mFit = aConfig.fit;

    // Stream.
    mImpl->mStream = ::avformat_new_stream(mImpl->mFmt, nullptr);
    if (!mImpl->mStream) { return fail("could not create stream"); }
    mImpl->mStream->id = mImpl->mFmt->nb_streams - 1;
    if (::avcodec_parameters_from_context(mImpl->mStream->codecpar, mImpl->mCtx) < 0) {
        return fail("could not copy codec parameters");
    }
    mImpl->mStream->time_base = mImpl->mCtx->time_base;
    // Stream codec tag (e.g. "hvc1" for HEVC). This belongs to the container's
    // stream entry, not the encoder, so it is applied AFTER the parameters are
    // copied and before the header is written. Without it, HEVC in an ISO
    // container defaults to hev1, which Safari/iOS refuse to play.
    if (aConfig.codecTag.size() == 4) {
        // Bind the UTF-8 buffer to a local: taking constData() off a temporary
        // QByteArray leaves a dangling pointer by the next statement (this
        // exact bug already bit the container name above).
        const QByteArray tagBytes = aConfig.codecTag.toUtf8();
        const char* t = tagBytes.constData();
        mImpl->mStream->codecpar->codec_tag = (uint32_t)MKTAG(t[0], t[1], t[2], t[3]);
    }
    // Make the real framerate discoverable to decoders/players (avg_frame_rate
    // is not the same as time_base and would otherwise read as 0/0).
    mImpl->mStream->avg_frame_rate = AVRational{ aConfig.fps, 1 };
    mImpl->mStream->r_frame_rate = AVRational{ aConfig.fps, 1 };

    // Optional audio stream (muxed in the same pass). Configured before the
    // container header so it is written with both streams.
    if (!aConfig.audioCodec.isEmpty() && aConfig.audioChannels > 0 && aConfig.audioSampleRate > 0) {
        if (!openAudio(aConfig)) {
            return fail(mError);
        }
    }

    // Open the output container.
    if (::avio_open(&mImpl->mFmt->pb, aConfig.outputPath.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
        return fail("could not open output file: " + aConfig.outputPath);
    }
    // Muxer options (faststart and friends) belong to the container, so they are
    // consumed HERE and not by the codec. Leftovers are reported: a requirement
    // that quietly did nothing is how a platform rejects a file we claimed was
    // built for it.
    AVDictionary* muxOpts = nullptr;
    for (auto it = aConfig.muxerOptions.constBegin(); it != aConfig.muxerOptions.constEnd(); ++it) {
        ::av_dict_set(&muxOpts, it.key().toUtf8().constData(), it.value().toUtf8().constData(), 0);
    }
    const int hdrErr = ::avformat_write_header(mImpl->mFmt, &muxOpts);
    if (muxOpts) {
        AVDictionaryEntry* leftover = ::av_dict_get(muxOpts, "", nullptr, AV_DICT_IGNORE_SUFFIX);
        const QString unknown = leftover ? QString::fromUtf8(leftover->key) : QString();
        ::av_dict_free(&muxOpts);
        if (leftover) {
            return fail(QStringLiteral("unknown muxer option '%1' for %2")
                            .arg(unknown, aConfig.container));
        }
    }
    if (hdrErr < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        ::av_strerror(hdrErr, errbuf, sizeof(errbuf));
        return fail(QStringLiteral("could not write container header for %1/%2: %3")
                        .arg(aConfig.container, aConfig.codec, QString::fromUtf8(errbuf)));
    }

    // Format converter: QImage is normalized to RGBA before sws.
    mImpl->mSws = ::sws_getContext(width, height, AV_PIX_FMT_RGBA,
                                   width, height, pixFmt, SWS_BILINEAR,
                                   nullptr, nullptr, nullptr);
    if (!mImpl->mSws) { return fail("could not create swscale context"); }

    // Allocate the input frame.
    mImpl->mFrame = ::av_frame_alloc();
    if (!mImpl->mFrame) { return fail("could not allocate frame"); }
    mImpl->mFrame->format = pixFmt;
    mImpl->mFrame->width = width;
    mImpl->mFrame->height = height;
    if (::av_frame_get_buffer(mImpl->mFrame, 32) < 0) {
        return fail("could not allocate frame buffer");
    }

    mImpl->mClosed = false;
    mOpened = true;
    return true;
}

bool VideoEncoder::write(const QImage& aFrame) {
    if (!mOpened) { mError = "encoder not open"; return false; }
    return writeFrame(aFrame);
}

bool VideoEncoder::writeFrame(const QImage& aFrame) {
    QImage rgba = aFrame.convertToFormat(QImage::Format_RGBA8888);
    const int width = mImpl->mCtx->width;
    const int height = mImpl->mCtx->height;

    // Pad/Crop: reconcile the aspect ratio here (before the size-mismatch path
    // below) so the frame handed to sws is already exactly the encoded size.
    rgba = fitImage(rgba, QSize(width, height), mImpl->mFit);

    // The encoder may have rounded a YUV frame dimension up to an even size
    // (normalizedSize). If the caller fed an odd-sized QImage, the sws/direct
    // source must be padded to the exact frame size so we never read outside
    // the source buffer. Edge-replicate the last row/column for the extra
    // pixels (deterministic, no uninitialized data).
    if (rgba.width() != width || rgba.height() != height) {
        const int srcW = rgba.width();
        const int srcH = rgba.height();
        QImage padded(width, height, QImage::Format_RGBA8888);
        for (int y = 0; y < height; ++y) {
            const uchar* sy = rgba.constScanLine(std::min(y, srcH - 1));
            uchar* dy = padded.scanLine(y);
            for (int x = 0; x < width; ++x) {
                const uchar* sp = sy + 4 * std::min(x, srcW - 1);
                std::memcpy(dy + 4 * x, sp, 4);
            }
        }
        rgba = padded;
    }

    if (::av_frame_make_writable(mImpl->mFrame) < 0) {
        mError = "could not make frame writable";
        return false;
    }

    // Scale/convert RGBA source into the encoded frame.
    if (mImpl->mPixFmt == AV_PIX_FMT_RGBA && rgba.bytesPerLine() == width * 4) {
        // Same format: direct copy (pixel-exact, no sws pass).
        const uchar* src = rgba.constBits();
        for (int y = 0; y < height; ++y) {
            std::memcpy(mImpl->mFrame->data[0] + y * mImpl->mFrame->linesize[0],
                        src + y * rgba.bytesPerLine(), size_t(width) * 4);
        }
    } else {
        const uint8_t* srcData[4] = { rgba.constBits(), nullptr, nullptr, nullptr };
        const int srcStride[4] = { static_cast<int>(rgba.bytesPerLine()), 0, 0, 0 };
        ::sws_scale(mImpl->mSws, srcData, srcStride, 0, height,
                    mImpl->mFrame->data, mImpl->mFrame->linesize);
    }

    mImpl->mFrame->pts = mWrittenFrames;
    ++mWrittenFrames;

    const int sendRet = ::avcodec_send_frame(mImpl->mCtx, mImpl->mFrame);
    if (sendRet < 0 && sendRet != AVERROR(EAGAIN)) {
        mError = "avcodec_send_frame failed";
        return false;
    }
    return drainEncoder();
}

bool VideoEncoder::drainEncoder() {
    AVPacket* pkt = ::av_packet_alloc();
    if (!pkt) { mError = "could not allocate packet"; return false; }

    bool ok = true;
    int ret;
    while ((ret = ::avcodec_receive_packet(mImpl->mCtx, pkt)) >= 0) {
        ::av_packet_rescale_ts(pkt, mImpl->mCtx->time_base, mImpl->mStream->time_base);
        pkt->stream_index = mImpl->mStream->index;
        if (::av_interleaved_write_frame(mImpl->mFmt, pkt) < 0) {
            mError = "could not write packet";
            ok = false;
            break;
        }
        ::av_packet_unref(pkt);
    }
    if (ok && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        mError = "avcodec_receive_packet error";
        ok = false;
    }
    ::av_packet_free(&pkt);
    return ok;
}

bool VideoEncoder::writeAll(const QList<QImage>& aFrames, const Config& aConfig) {
    if (!open(aConfig))
        return false;
    for (const QImage& f : aFrames) {
        if (!write(f))
            return false;
    }
    return close();
}

bool VideoEncoder::openAudio(const Config& aConfig) {
    const AVCodec* audioCodec = ::avcodec_find_encoder_by_name(aConfig.audioCodec.toUtf8().constData());
    if (!audioCodec) {
        mError = "audio encoder not found: " + aConfig.audioCodec;
        return false;
    }
    mImpl->mAudioCtx = ::avcodec_alloc_context3(audioCodec);
    if (!mImpl->mAudioCtx) { mError = "could not allocate audio codec context"; return false; }

    const int rate = aConfig.audioSampleRate;
    const int ch = aConfig.audioChannels;

    mImpl->mAudioCtx->sample_fmt = codecSampleFmt(audioCodec);
    mImpl->mAudioCtx->sample_rate = rate;
    AVChannelLayout layout{};
    ::av_channel_layout_default(&layout, ch);
    ::av_channel_layout_copy(&mImpl->mAudioCtx->ch_layout, &layout);
    ::av_channel_layout_uninit(&layout);
    if (aConfig.audioBitrate > 0)
        mImpl->mAudioCtx->bit_rate = aConfig.audioBitrate;
    mImpl->mAudioCtx->time_base = AVRational{ 1, rate };

    if (::avcodec_open2(mImpl->mAudioCtx, audioCodec, nullptr) < 0) {
        mError = "could not open audio encoder: " + aConfig.audioCodec;
        return false;
    }

    // Convert the AudioMixer's interleaved float into the codec's sample
    // format (AAC wants planar fltp; opus will take packed flt/s16).
    AVChannelLayout inL{};
    ::av_channel_layout_default(&inL, ch);
    if (::swr_alloc_set_opts2(&mImpl->mAudioSwr, &mImpl->mAudioCtx->ch_layout,
                              mImpl->mAudioCtx->sample_fmt, rate,
                              &inL, AV_SAMPLE_FMT_FLT, rate, 0, nullptr) < 0 ||
        ::swr_init(mImpl->mAudioSwr) < 0) {
        ::av_channel_layout_uninit(&inL);
        mError = "could not init audio resampler";
        return false;
    }
    ::av_channel_layout_uninit(&inL);
    mImpl->mAudioFmt = mImpl->mAudioCtx->sample_fmt;

    mImpl->mAudioStream = ::avformat_new_stream(mImpl->mFmt, nullptr);
    if (!mImpl->mAudioStream) { mError = "could not create audio stream"; return false; }
    mImpl->mAudioStream->id = mImpl->mFmt->nb_streams - 1;
    if (::avcodec_parameters_from_context(mImpl->mAudioStream->codecpar, mImpl->mAudioCtx) < 0) {
        mError = "could not copy audio codec parameters"; return false;
    }
    mImpl->mAudioStream->time_base = mImpl->mAudioCtx->time_base;

    mImpl->mAudioChannels = ch;
    mImpl->mAudioRate = rate;
    mImpl->mAudioCodec = aConfig.audioCodec;
    return true;
}

bool VideoEncoder::setAudio(const float* aInterleaved, int aChannels, int aSampleRate,
                            qint64 aSampleCountPerChannel) {    if (!mOpened || !mImpl->mAudioCtx) { mError = "audio not configured (set audioCodec/channels/rate)"; return false; }
    if (aChannels != mImpl->mAudioChannels || aSampleRate != mImpl->mAudioRate) {
        mError = "audio sample rate/channels do not match the configured stream";
        return false;
    }
    if (aSampleCountPerChannel <= 0 || aChannels <= 0) { mError = "empty audio data"; return false; }
    const qint64 total = aSampleCountPerChannel * aChannels;
    mImpl->mAudio.assign(aInterleaved, aInterleaved + total);
    mImpl->mAudioCount = aSampleCountPerChannel;
    return true;
}

bool VideoEncoder::hasAudio() const { return mImpl->mAudioCtx != nullptr; }

bool VideoEncoder::writeAudio() {
    if (!mImpl->mAudioCtx || mImpl->mAudioCount <= 0)
        return true; // no audio to write

    const int frameSize = mImpl->mAudioCtx->frame_size > 0 ? mImpl->mAudioCtx->frame_size : 1024;
    const int ch = mImpl->mAudioChannels;

    // Pad the interleaved buffer up to a whole number of encoder frames so the
    // last (partial) frame is filled with silence (AAC needs full frames).
    const qint64 aligned = (mImpl->mAudioCount + frameSize - 1) / frameSize * frameSize;
    QVector<float> padded(qsizetype(aligned) * ch, 0.0f);
    std::memcpy(padded.data(), mImpl->mAudio.constData(),
                size_t(mImpl->mAudioCount) * ch * sizeof(float));

    AVFrame* aframe = ::av_frame_alloc();
    if (!aframe) { mError = "could not allocate audio frame"; return false; }
    aframe->format = mImpl->mAudioFmt;
    aframe->nb_samples = frameSize;
    aframe->sample_rate = mImpl->mAudioRate;
    ::av_channel_layout_copy(&aframe->ch_layout, &mImpl->mAudioCtx->ch_layout);
    if (::av_frame_get_buffer(aframe, 0) < 0) {
        ::av_frame_free(&aframe);
        mError = "could not allocate audio frame buffer";
        return false;
    }

    bool ok = true;

    // Take everything the encoder is currently holding. EAGAIN just means "not
    // right now" and EOF "no more ever" — neither is an error.
    auto drain = [&]() -> bool {
        AVPacket* pkt = ::av_packet_alloc();
        if (!pkt) { mError = "could not allocate audio packet"; return false; }
        int ret = 0;
        bool good = true;
        while ((ret = ::avcodec_receive_packet(mImpl->mAudioCtx, pkt)) >= 0) {
            ::av_packet_rescale_ts(pkt, mImpl->mAudioCtx->time_base, mImpl->mAudioStream->time_base);
            pkt->stream_index = mImpl->mAudioStream->index;
            if (::av_interleaved_write_frame(mImpl->mFmt, pkt) < 0) {
                mError = "could not write audio packet";
                good = false;
                ::av_packet_unref(pkt);
                break;
            }
            ::av_packet_unref(pkt);
        }
        if (good && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            mError = "audio receive error";
            good = false;
        }
        ::av_packet_free(&pkt);
        return good;
    };

    const qint64 totalFrames = aligned / frameSize;
    for (qint64 f = 0; f < totalFrames && ok; ++f) {
        if (::av_frame_make_writable(aframe) < 0) { mError = "audio frame not writable"; ok = false; break; }
        const uint8_t* inArr[1] = { reinterpret_cast<const uint8_t*>(padded.constData() + f * frameSize * ch) };
        const int n = ::swr_convert(mImpl->mAudioSwr, aframe->data, frameSize,
                                    inArr, frameSize);
        if (n < 0) { mError = "audio resample failed"; ok = false; break; }
        aframe->nb_samples = n;
        // PTS in the ENCODER's time base, which for audio is 1/sample_rate — so a
        // frame's timestamp is its first SAMPLE, not its index. `pts = f` put the
        // whole stream inside three samples of t=0: the samples were all encoded
        // and all written, and the muxed stream still reported one frame's duration
        // (0.021s) however long the video was, because every packet claimed to be
        // at the same instant. Content was right; time was collapsed.
        aframe->pts = f * frameSize;

        // EAGAIN from send_frame means the encoder is full. The previous version
        // only checked `>= 0`, so an EAGAIN frame's audio was dropped on the floor
        // with no symptom other than a file that was too short. Drain and retry.
        int ret = ::avcodec_send_frame(mImpl->mAudioCtx, aframe);
        while (ret == AVERROR(EAGAIN)) {
            if (!drain()) { ok = false; break; }
            ret = ::avcodec_send_frame(mImpl->mAudioCtx, aframe);
        }
        if (!ok) break;
        if (ret < 0) { mError = "audio send error"; ok = false; break; }
        if (!drain()) { ok = false; break; }
    }

    // Flush the encoder. It holds the tail of the stream — for AAC that is the
    // priming delay plus whatever is buffered, which is why exported audio was
    // measured at ONE frame (0.021s) regardless of how long the video was. Without
    // this send(nullptr) the rest never reaches the file.
    if (ok) {
        ::avcodec_send_frame(mImpl->mAudioCtx, nullptr);
        if (!drain()) ok = false;
    }

    ::av_frame_free(&aframe);
    return ok;
}

bool VideoEncoder::close() {
    if (!mOpened && mImpl->mClosed)
        return true;

    bool ok = true;
    if (mOpened) {
        // Flush the video encoder (send NULL, drain until EOF).
        ::avcodec_send_frame(mImpl->mCtx, nullptr);
        ok = drainEncoder();
        // Encode + mux the buffered audio (if any).
        if (ok && !writeAudio())
            ok = false;
        if (::av_write_trailer(mImpl->mFmt) < 0)
            ok = false;
    }

    cleanup();
    return ok;
}

void VideoEncoder::cleanup() {
    if (mImpl->mFrame) { ::av_frame_free(&mImpl->mFrame); }
    if (mImpl->mSws) { ::sws_freeContext(mImpl->mSws); mImpl->mSws = nullptr; }
    if (mImpl->mAudioSwr) { ::swr_free(&mImpl->mAudioSwr); mImpl->mAudioSwr = nullptr; }
    if (mImpl->mAudioCtx) { ::avcodec_free_context(&mImpl->mAudioCtx); mImpl->mAudioCtx = nullptr; }
    if (mImpl->mFmt) {
        if (mImpl->mFmt->pb) { ::avio_closep(&mImpl->mFmt->pb); }
        if (mImpl->mCtx) { ::avcodec_free_context(&mImpl->mCtx); }
        ::avformat_free_context(mImpl->mFmt);
        mImpl->mFmt = nullptr;
    }
    mImpl->mCtx = nullptr;
    mImpl->mStream = nullptr;
    mImpl->mAudioStream = nullptr;
    mImpl->mAudio.clear();
    mImpl->mAudioCount = 0;
    mImpl->mPixFmt = AV_PIX_FMT_NONE;
    mImpl->mClosed = true;
    mOpened = false;
}

} // namespace media
