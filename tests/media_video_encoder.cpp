// V1 — media::VideoEncoder correctness + edge cases.
//
// Verification-first: the encoder is exercised headless (no GUI, just Qt Gui
// for QImage), and output is round-tripped back through the SAME linked libav*
// decode path so we assert real fidelity, not a happy-path smoke test.
//
// Checks:
//   * lossless exact round-trip (ffv1 @ RGBA): decoded == source pixels.
//   * standard mp4 (libx264 @ yuv420p): valid, correct dims/fps/frame count.
//   * single frame, bad-param rejection, odd-size normalization.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/media_video_encoder
#include <QGuiApplication>
#include <QImage>
#include <QList>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QByteArray>
#include <QtMath>

#include "media/VideoEncoder.h"
#include "media/FFmpegHelpers.h"
#include "media/AudioMixer.h"
#include "media/GifEncoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <cmath>
#include <cstring>
#include <algorithm>

static int failures = 0;

static void check(bool ok, const char* what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// Deterministic synthetic RGBA frame: horizontal gradient + alpha ramp + a
// stepped color band so both luma and chroma vary (catches sws errors).
// Format_RGBA8888 stores bytes R,G,B,A (unpremultiplied); write them directly.
static QImage makeFrame(int w, int h, int idx) {
    QImage img(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            line[4 * x + 0] = uchar((x * 255) / (w == 0 ? 1 : w));                 // R
            line[4 * x + 1] = uchar((y * 255) / (h == 0 ? 1 : h));                 // G
            line[4 * x + 2] = uchar(((x + y + idx) * 255) / ((w + h) == 0 ? 1 : (w + h))); // B
            line[4 * x + 3] = uchar(255 - ((x + y) % 256));                        // A (ramp)
        }
    }
    return img;
}

static QImage makeSolid(int w, int h, const QColor& c) {
    QImage img(w, h, QImage::Format_RGBA8888);
    img.fill(c);
    return img;
}

static bool imagesEqual(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) return false;
    const QImage ra = a.convertToFormat(QImage::Format_RGBA8888);
    const QImage rb = b.convertToFormat(QImage::Format_RGBA8888);
    const int rowBytes = ra.width() * 4;
    for (int y = 0; y < ra.height(); ++y) {
        if (std::memcmp(ra.constBits() + y * ra.bytesPerLine(),
                        rb.constBits() + y * rb.bytesPerLine(),
                        size_t(rowBytes)) != 0)
            return false;
    }
    return true;
}

// Decode every video frame of a file back into RGBA QImages.
static QList<QImage> decodeFrames(const QString& path, int* outW, int* outH, int* outFps) {
    QList<QImage> frames;
    AVFormatContext* fmt = nullptr;
    if (::avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return frames;
    ::avformat_find_stream_info(fmt, nullptr);
    const int vs = ::av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vs < 0) { ::avformat_close_input(&fmt); return frames; }

    const AVCodec* dec = ::avcodec_find_decoder(fmt->streams[vs]->codecpar->codec_id);
    AVCodecContext* ctx = ::avcodec_alloc_context3(dec);
    ::avcodec_parameters_to_context(ctx, fmt->streams[vs]->codecpar);
    if (::avcodec_open2(ctx, dec, nullptr) < 0) { ::avformat_close_input(&fmt); return frames; }

    *outW = ctx->width;
    *outH = ctx->height;
    *outFps = fmt->streams[vs]->avg_frame_rate.num
                  ? fmt->streams[vs]->avg_frame_rate.num /
                        (fmt->streams[vs]->avg_frame_rate.den ? fmt->streams[vs]->avg_frame_rate.den : 1)
                  : 0;

    SwsContext* sws = ::sws_getContext(ctx->width, ctx->height, ctx->pix_fmt,
                                       ctx->width, ctx->height, AV_PIX_FMT_RGBA,
                                       SWS_BILINEAR, nullptr, nullptr, nullptr);
    AVFrame* frame = ::av_frame_alloc();
    AVPacket* pkt = ::av_packet_alloc();

    auto pull = [&](AVFrame* f) {
        QImage img(ctx->width, ctx->height, QImage::Format_RGBA8888);
        uint8_t* dst[4] = { img.bits(), nullptr, nullptr, nullptr };
        const int dstStride[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };
        ::sws_scale(sws, f->data, f->linesize, 0, ctx->height, dst, dstStride);
        frames.append(img);
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
    ::sws_freeContext(sws);
    ::avcodec_free_context(&ctx);
    ::avformat_close_input(&fmt);
    return frames;
}

static bool run_lossless_roundtrip() {
    const QList<QImage> src = { makeFrame(64, 48, 0), makeFrame(64, 48, 1), makeFrame(64, 48, 2) };
    const QString out = QDir::temp().filePath("ae_v1_lossless.avi");

    // Lossless codec that accepts 8-bit BGRA (huffyuv is lossless). We feed
    // RGBA; the RGBA<->BGRA sws shuffle is a byte reorder that cancels out on
    // the round-trip, so the decoded frames must match the source exactly.
    media::VideoEncoder::Config cfg;
    cfg.outputPath = out;
    cfg.container = "avi";
    cfg.codec = "huffyuv";
    cfg.width = 64;
    cfg.height = 48;
    cfg.fps = 24;
    cfg.pixFmt = "bgra";

    media::VideoEncoder enc;
    if (!enc.writeAll(src, cfg)) {
        check(false, "lossless encode failed");
        QFile::remove(out);
        return false;
    }

    int w = 0, h = 0, fps = 0;
    const QList<QImage> dec = decodeFrames(out, &w, &h, &fps);
    check(w == 64 && h == 48, "lossless decode dims match");
    check(dec.size() == src.size(), "lossless frame count matches");
    if (dec.size() == src.size()) {
        bool exact = true;
        for (int i = 0; i < dec.size(); ++i)
            if (!imagesEqual(src[i], dec[i])) { exact = false; break; }
        check(exact, "lossless round-trip is pixel-exact (RGBA)");
    }
    QFile::remove(out);
    return true;
}

static bool run_mp4_x264() {
    const QList<QImage> src = { makeFrame(320, 240, 0), makeFrame(320, 240, 1), makeFrame(320, 240, 2),
                                makeFrame(320, 240, 3), makeFrame(320, 240, 4) };
    const QString out = QDir::temp().filePath("ae_v1_x264.mp4");

    media::VideoEncoder::Config cfg;
    cfg.outputPath = out;
    cfg.container = "mp4";
    cfg.codec = "libx264";
    cfg.width = 320;
    cfg.height = 240;
    cfg.fps = 24;
    cfg.pixFmt = "yuv420p";

    media::VideoEncoder enc;
    if (!enc.writeAll(src, cfg)) {
        check(false, "libx264 encode failed");
        QFile::remove(out);
        return false;
    }

    int w = 0, h = 0, fps = 0;
    const QList<QImage> dec = decodeFrames(out, &w, &h, &fps);
    check(w == 320 && h == 240, "libx264 decode dims match");
    check(fps == 24, "libx264 fps matches");
    check(dec.size() == src.size(), "libx264 frame count matches");
    QFile::remove(out);
    return true;
}

static bool run_single_frame() {
    const QList<QImage> src = { makeFrame(80, 60, 0) };
    const QString out = QDir::temp().filePath("ae_v1_single.png.mp4");
    media::VideoEncoder enc;
    media::VideoEncoder::Config cfg;
    cfg.outputPath = out; cfg.container = "mp4"; cfg.codec = "ffv1";
    cfg.width = 80; cfg.height = 60; cfg.fps = 10; cfg.pixFmt = "rgba";
    check(enc.writeAll(src, cfg), "single-frame encode succeeds");
    int w = 0, h = 0, fps = 0;
    const QList<QImage> dec = decodeFrames(out, &w, &h, &fps);
    check(dec.size() == 1 && w == 80 && h == 60, "single-frame decode succeeds");
    QFile::remove(out);
    return true;
}

static bool run_bad_params() {
    media::VideoEncoder enc;
    media::VideoEncoder::Config cfg;
    cfg.outputPath = QDir::temp().filePath("ae_v1_bad.mp4");
    cfg.container = "mp4"; cfg.codec = "libx264"; cfg.width = 64; cfg.height = 64; cfg.fps = 0; // invalid
    check(!enc.open(cfg), "fps=0 rejected (open returns false)");
    return true;
}

static bool run_odd_size_norm() {
    const QSize n = media::VideoEncoder::normalizedSize(QSize(321, 241), "yuv420p");
    check(n.width() == 322 && n.height() == 242, "odd yuv size normalized to even (322x242)");
    return true;
}

static bool run_container_coverage() {
    struct Case { const char* container; const char* codec; const char* pixFmt; };
    const Case cases[] = {
        { "mp4",  "libx264",   "yuv420p" },
        { "webm", "libvpx-vp9", "yuv420p" },
        { "mov",  "prores_ks", "" },
        { "avi",  "mpeg4",     "yuv420p" },
        { "ogv",  "libtheora", "yuv420p" },
        { "mkv",  "ffv1",      "bgra" },
    };
    const QList<QImage> src = { makeFrame(128, 96, 0), makeFrame(128, 96, 1) };
    bool all = true;
    for (const auto& c : cases) {
        if (!media::ffmpeg::encoderAvailable(c.codec)) {
            QTextStream(stdout) << "SKIP  " << c.container << "/" << c.codec
                                << " (encoder absent in this build)\n";
            continue;
        }
        const QString out = QDir::temp().filePath(QString("ae_v1_%1.%2").arg(c.container, c.container));
        media::VideoEncoder enc;
        media::VideoEncoder::Config cfg;
        cfg.outputPath = out; cfg.container = c.container; cfg.codec = c.codec;
        cfg.width = 128; cfg.height = 96; cfg.fps = 15; cfg.pixFmt = c.pixFmt;
        bool ok = enc.writeAll(src, cfg);
        int w = 0, h = 0, fps = 0;
        const QList<QImage> dec = ok ? decodeFrames(out, &w, &h, &fps) : QList<QImage>();
        const QSize norm = media::VideoEncoder::normalizedSize(QSize(128, 96), c.pixFmt);
        // Valid decodable file with the right dimensions. Some codecs (theora)
        // introduce a 1-frame delay, so do not assert an exact count here —
        // exact count correctness is covered by the lossless + libx264 tests.
        bool ok2 = ok && !dec.isEmpty() && w == norm.width() && h == norm.height();
        QTextStream(stdout) << (ok2 ? "PASS  " : "FAIL  ")
                            << c.container << "/" << c.codec
                            << (ok ? ("  -> " + QString::number(dec.size()) + " frames, " +
                                      QString::number(w) + "x" + QString::number(h)) : "  (encode failed)")
                            << "\n";
        if (!ok2) all = false;
        QFile::remove(out);
    }
    check(all, "all available container/codec pairs encode + decode correctly");
    return all;
}

// Open a media file and report whether it has video + audio streams, and the
// RMS energy of the decoded audio (so we know the audio stream is not silent).
static bool probeStreams(const QString& path, bool* hasVideo, bool* hasAudio, double* audioRms) {
    *hasVideo = *hasAudio = false;
    *audioRms = 0.0;
    AVFormatContext* fmt = nullptr;
    if (::avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return false;
    ::avformat_find_stream_info(fmt, nullptr);
    const int vs = ::av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int as = ::av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    *hasVideo = vs >= 0;
    *hasAudio = as >= 0;

    if (as >= 0) {
        const AVCodec* dec = ::avcodec_find_decoder(fmt->streams[as]->codecpar->codec_id);
        AVCodecContext* ctx = ::avcodec_alloc_context3(dec);
        ::avcodec_parameters_to_context(ctx, fmt->streams[as]->codecpar);
        ::avcodec_open2(ctx, dec, nullptr);
        AVFrame* fr = ::av_frame_alloc();
        AVPacket* pk = ::av_packet_alloc();
        double sum = 0.0;
        int n = 0;
        const int limit = 200000;
        while (::av_read_frame(fmt, pk) >= 0) {
            if (pk->stream_index == as) {
                ::avcodec_send_packet(ctx, pk);
                while (::avcodec_receive_frame(ctx, fr) >= 0) {
                    const int ch = fr->ch_layout.nb_channels > 0 ? fr->ch_layout.nb_channels : 1;
                    const bool planar = ::av_sample_fmt_is_planar(static_cast<AVSampleFormat>(fr->format));
                    if (planar) {
                        for (int c = 0; c < ch && n < limit; ++c) {
                            const float* d = reinterpret_cast<const float*>(fr->extended_data[c]);
                            for (int i = 0; i < fr->nb_samples && n < limit; ++i, ++n)
                                sum += double(d[i]) * d[i];
                        }
                    } else {
                        const float* d = reinterpret_cast<const float*>(fr->extended_data[0]);
                        for (int i = 0; i < fr->nb_samples * ch && n < limit; ++i, ++n)
                            sum += double(d[i]) * d[i];
                    }
                    if (n >= limit) break;
                }
            }
            ::av_packet_unref(pk);
            if (n >= limit) break;
        }
        ::av_frame_free(&fr);
        ::av_packet_free(&pk);
        ::avcodec_free_context(&ctx);
        *audioRms = n > 0 ? std::sqrt(sum / n) : 0.0;
    }
    ::avformat_close_input(&fmt);
    return true;
}

// Mux a video stream (frames) + one audio stream (from AudioMixer) in a single
// pass, and verify both streams + that the audio is non-silent.
static bool run_video_audio_mux() {
    const QString out = QDir::temp().filePath("ae_v3_mux.mp4");
    const QString wav = QDir::temp().filePath("ae_v3_tone.wav");

    // Generate a stereo 48k sine WAV (1 s), then mix it as the audio track.
    const int rate = 48000;
    QFile wf(wav);
    if (wf.open(QIODevice::WriteOnly)) {
        const int n = rate; // 1 s of samples
        const int block = 4; // stereo 16-bit
        auto w32 = [&](quint32 v) { wf.write(reinterpret_cast<const char*>(&v), 4); };
        auto w16 = [&](quint16 v) { wf.write(reinterpret_cast<const char*>(&v), 2); };
        wf.write("RIFF", 4); w32(quint32(36 + n * block)); wf.write("WAVE", 4);
        wf.write("fmt ", 4); w32(16); w16(1); w16(2); w32(quint32(rate));
        w32(quint32(rate * block)); w16(quint16(block)); w16(16);
        wf.write("data", 4); w32(quint32(n * block));
        for (int i = 0; i < n; ++i) {
            const qint16 v = qint16(0.5 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * i / rate) * 32767.0);
            w16(quint16(v)); w16(quint16(v));
        }
    }

    media::AudioMixer::Track tr; tr.path = wav; tr.startFrame = 0; tr.volume = 1.0f;
    media::AudioMixer mx;
    media::AudioMixer::Config acfg;
    acfg.fps = 24; acfg.totalFrames = 24; acfg.sampleRate = rate; acfg.channels = 2;
    check(mx.mix({ tr }, acfg), "video+audio: audio mix succeeds");

    const QList<QImage> frames = { makeFrame(160, 120, 0), makeFrame(160, 120, 1), makeFrame(160, 120, 2) };

    media::VideoEncoder enc;
    media::VideoEncoder::Config cfg;
    cfg.outputPath = out; cfg.container = "mp4"; cfg.codec = "libx264";
    cfg.width = 160; cfg.height = 120; cfg.fps = 24; cfg.pixFmt = "yuv420p";
    cfg.audioCodec = "aac"; cfg.audioChannels = 2; cfg.audioSampleRate = rate; cfg.audioBitrate = 128000;
    check(enc.open(cfg), "video+audio: open with audio succeeds");
    bool wrote = true;
    for (const QImage& f : frames) if (!enc.write(f)) { wrote = false; break; }
    check(wrote, "video+audio: video frames written");
    check(enc.setAudio(mx.samples(), mx.channels(), mx.sampleRate(), mx.sampleCount()),
          "video+audio: setAudio succeeds");
    check(enc.close(), "video+audio: close succeeds");

    bool hasV = false, hasA = false;
    double rms = 0.0;
    probeStreams(out, &hasV, &hasA, &rms);
    check(hasV, "video+audio: output has a video stream");
    check(hasA, "video+audio: output has an audio stream");
    check(rms > 1e-3, "video+audio: audio stream is non-silent (RMS > 1e-3)");

    QFile::remove(out); QFile::remove(wav);
    return true;
}

// Verify the preset -> VideoEncoder::Config bridge (makeConfig) that the
// exporter uses, including a live encode from a makeConfig-built config.
static bool run_make_config() {
    QString err;
    auto c = media::VideoEncoder::makeConfig("mp4", "libx264", "yuv420p", false,
                                             320, 240, 24, 2000, &err);
    check(c.codec == QLatin1String("libx264") && c.bitrate == 2000 * 1000 &&
              c.pixFmt == QLatin1String("yuv420p") && c.container == QLatin1String("mp4"),
          "makeConfig: mp4/libx264/yuv420p maps (codec, bitrate kbps->bps, pixFmt, container)");
    check(err.isEmpty(), "makeConfig: no error for a valid preset");

    auto t = media::VideoEncoder::makeConfig("ogv", "theora", "yuv420p", false,
                                             320, 240, 24, 0, &err);
    check(t.codec == QLatin1String("libtheora"),
          "makeConfig: friendly alias theora -> libtheora");

    auto tr = media::VideoEncoder::makeConfig("mp4", "libx264", "", true,
                                              320, 240, 24, 0, &err);
    // The transparent hint flows through; the actual alpha format is chosen at
    // encoder open() (the build's libx264 may not report yuva420p here).
    check(tr.transparent, "makeConfig: transparent hint passes through");

    // allowTransparency auto-picks an alpha format when the codec supports one
    // (ffv1 lists yuva420p but not rgba). This wires the dialog's "Allow
    // transparency" checkbox to a real alpha stream when the codec can do it.
    auto ta = media::VideoEncoder::makeConfig("mkv", "ffv1", "", true,
                                               320, 240, 24, 0, &err);
    check(ta.codec == QLatin1String("ffv1"), "makeConfig: transparent ffv1 resolves");
    check(ta.pixFmt == QLatin1String("yuva420p"),
          "makeConfig: transparent + empty pixfmt picks an alpha format (yuva420p)");

    QString err2;
    auto bad = media::VideoEncoder::makeConfig("mp4", "definitely_not_a_codec", "", false,
                                               320, 240, 24, 0, &err2);
    check(bad.codec.isEmpty() && !err2.isEmpty(),
          "makeConfig: unknown encoder rejected (empty codec + error)");

    // Live: build a config via makeConfig, then encode + decode-verify.
    auto cfg = media::VideoEncoder::makeConfig("mp4", "libx264", "yuv420p", false,
                                               64, 64, 10, 1000, &err);
    cfg.outputPath = QDir::temp().filePath("ae_v4_cfg.mp4");
    media::VideoEncoder enc;
    const QList<QImage> frames = { makeFrame(64, 64, 0), makeFrame(64, 64, 1) };
    check(enc.writeAll(frames, cfg), "makeConfig-built config encodes successfully");
    int w = 0, h = 0, fps = 0;
    const QList<QImage> dec = decodeFrames(cfg.outputPath, &w, &h, &fps);
    check(dec.size() == 2 && w == 64 && h == 64, "makeConfig-built config decodes correctly");
    QFile::remove(cfg.outputPath);
    return true;
}

// Decode the FIRST frame of a file using a SPECIFIC decoder name (libvpx for VP9,
// which preserves the alpha channel the native FFmpeg decoder discards), and
// return it as RGBA + whether the alpha channel is non-uniform (real alpha).
static bool decodeFirstAlpha(const QString& path, const char* aDecoderName, int* oMinA, int* oMaxA) {
    AVFormatContext* fmt = nullptr;
    if (::avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) return false;
    ::avformat_find_stream_info(fmt, nullptr);
    const int vs = ::av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vs < 0) { ::avformat_close_input(&fmt); return false; }

    const AVCodec* dec = ::avcodec_find_decoder_by_name(aDecoderName);
    AVCodecContext* ctx = ::avcodec_alloc_context3(dec);
    ::avcodec_parameters_to_context(ctx, fmt->streams[vs]->codecpar);
    if (::avcodec_open2(ctx, dec, nullptr) < 0) { ::avformat_close_input(&fmt); return false; }

    AVFrame* frame = ::av_frame_alloc();
    AVPacket* pkt = ::av_packet_alloc();
    int minA = 255, maxA = 0; bool got = false;
    while (::av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vs) {
            ::avcodec_send_packet(ctx, pkt);
            while (::avcodec_receive_frame(ctx, frame) >= 0) {
                // The alpha plane is only present/live in a yuva* frame; create
                // the sws context from the FRAME format (codecpar reports
                // yuv420p even when alpha side-data is carried).
                const AVPixelFormat fFmt = static_cast<AVPixelFormat>(frame->format);
                const AVPixFmtDescriptor* fdesc = ::av_pix_fmt_desc_get(fFmt);
                const bool isAlpha = fdesc && (fdesc->flags & AV_PIX_FMT_FLAG_ALPHA);
                if (isAlpha) {
                    SwsContext* sws = ::sws_getContext(ctx->width, ctx->height, fFmt,
                                                       ctx->width, ctx->height, AV_PIX_FMT_RGBA,
                                                       SWS_BILINEAR, nullptr, nullptr, nullptr);
                    QImage img(ctx->width, ctx->height, QImage::Format_RGBA8888);
                    uint8_t* dst[4] = { img.bits(), nullptr, nullptr, nullptr };
                    const int dstStride[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };
                    ::sws_scale(sws, frame->data, frame->linesize, 0, ctx->height, dst, dstStride);
                    ::sws_freeContext(sws);
                    for (int i = 3; i < img.width() * img.height() * 4; i += 4) {
                        const int a = img.constBits()[i];
                        minA = std::min(minA, a); maxA = std::max(maxA, a);
                    }
                    got = true;
                }
                break;
            }
        }
        ::av_packet_unref(pkt);
        if (got) break;
    }
    ::av_frame_free(&frame);
    ::av_packet_free(&pkt);
    ::avcodec_free_context(&ctx);
    ::avformat_close_input(&fmt);
    *oMinA = minA; *oMaxA = maxA;
    return got;
}

// Edge case: VP9/WebM alpha. Single-pass libvpx-vp9 @ yuva420p writes real alpha
// as packet side data (alpha_mode=1); the alpha is only recovered by the libvpx
// DECODER (the native FFmpeg VP9 decoder discards it, which is why ffprobe
// reports yuv420p). This proves the in-process path carries WebM alpha.
static bool run_vp9_webm_alpha() {
    if (!media::ffmpeg::encoderAvailable("libvpx-vp9")) {
        QTextStream(stdout) << "SKIP  vp9 webm alpha (libvpx-vp9 absent)\n";
        return true;
    }
    const int w = 64, h = 48;
    QList<QImage> src;
    for (int i = 0; i < 3; ++i) src.append(makeFrame(w, h, i));

    const QString out = QDir::temp().filePath("ae_v1_vp9a.webm");
    media::VideoEncoder::Config cfg;
    cfg.outputPath = out; cfg.container = "webm"; cfg.codec = "libvpx-vp9";
    cfg.width = w; cfg.height = h; cfg.fps = 24; cfg.pixFmt = "yuva420p"; cfg.transparent = true;

    media::VideoEncoder enc;
    bool ok = enc.writeAll(src, cfg);
    check(ok, "vp9 webm alpha: encode succeeds (libvpx-vp9 @ yuva420p)");

    int minA = 255, maxA = 0;
    bool got = ok && decodeFirstAlpha(out, "libvpx-vp9", &minA, &maxA);
    check(got, "vp9 webm alpha: decoded frame recovered via libvpx decoder");
    check(got && minA < 200 && maxA > 50 && minA != maxA,
          "vp9 webm alpha: alpha channel preserved (non-uniform)");
    QFile::remove(out);
    return true;
}

// Edge case: alpha in APNG and animated WebP (lossless, alpha-native codecs).
// APNG round-trips alpha through the native decoder. WebP the encoder emits a
// valid animated WebP (ANIM/ANMF + alpha) that browsers read, but FFmpeg's
// libwebp DECODER has no animated-WebP support (it skips ANIM/ANMF), so it
// cannot be round-tripped in-process — we assert a non-empty well-formed file.
static bool run_png_anim_alpha() {
    struct C { const char* cont; const char* codec; const char* decoder; const char* file; bool decode; };
    const C cases[] = {
        { "apng", "apng", "apng", "ae_v1_apnga.png", true },
        { "webp", "libwebp", "libwebp", "ae_v1_webpa.webp", false },
    };
    const int w = 64, h = 48;
    QList<QImage> src;
    for (int i = 0; i < 3; ++i) src.append(makeFrame(w, h, i));
    bool all = true;
    for (const auto& c : cases) {
        if (!media::ffmpeg::encoderAvailable(c.codec)) {
            QTextStream(stdout) << "SKIP  " << c.codec << " (absent)\n";
            continue;
        }
        const QString out = QDir::temp().filePath(c.file);
        media::VideoEncoder::Config cfg;
        cfg.outputPath = out; cfg.container = c.cont; cfg.codec = c.codec;
        cfg.width = w; cfg.height = h; cfg.fps = 10; cfg.pixFmt = "rgba"; cfg.transparent = true;
        media::VideoEncoder enc;
        bool ok = enc.writeAll(src, cfg);
        bool alphaOk = false;
        int minA = 255, maxA = 0;
        if (c.decode) {
            bool got = ok && decodeFirstAlpha(out, c.decoder, &minA, &maxA);
            alphaOk = got && minA < 200 && maxA > 50 && minA != maxA;
        } else {
            // Animated WebP: FFmpeg can't decode it back; assert a valid file was
            // written (the ANIM/ANMF + alpha stream is what browsers consume).
            alphaOk = ok && QFile::exists(out) && QFile(out).size() > 0;
        }
        QTextStream(stdout) << (alphaOk ? "PASS  " : "FAIL  ")
                            << c.cont << "/" << c.codec
                            << (c.decode ? (" alpha min=" + QString::number(minA) +
                                             " max=" + QString::number(maxA))
                                          : " (animated webp: file written; no in-process decode)")
                            << "\n";
        if (!alphaOk) all = false;
        QFile::remove(out);
    }
    check(all, "APNG (round-trip alpha) + WebP (valid animated output) exported");
    return all;
}

// In-process GIF encode (media::GifEncoder): valid .gif, correct frame count/dims.
static bool run_gif() {
    const QList<QImage> frames = { makeFrame(64, 48, 0), makeFrame(64, 48, 1), makeFrame(64, 48, 2) };
    const QString out = QDir::temp().filePath("ae_gif.gif");
    media::GifEncoder g;
    media::GifEncoder::Config c;
    c.outputPath = out; c.fps = 10; c.transparent = false;
    QString err;
    check(g.encode(frames, c, &err), "gif encode succeeds");
    check(QFile::exists(out) && QFile(out).size() > 0, "gif file exists + non-empty");
    int w = 0, h = 0, fps = 0;
    const QList<QImage> dec = decodeFrames(out, &w, &h, &fps);
    check(w == 64 && h == 48 && dec.size() == 3, "gif decodes to 3 frames at 64x48");
    QFile::remove(out);
    return true;
}

// Custom palette: a PNG whose pixels ARE the palette (paletteuse convention).
// The encoder must use those exact colors instead of an adaptive palette, and a
// fully-transparent pixel in the PNG reserves the transparent index.
static bool run_custom_palette_gif() {
    // Build a small palette PNG: 4 columns => the colors red/green/blue/white,
    // plus a fully-transparent pixel to reserve a transparent entry.
    const QString palPng = QDir::temp().filePath("ae_custom_pal.png");
    QImage palImg(3, 2, QImage::Format_RGBA8888);
    palImg.fill(Qt::transparent);
    // Row 0: red, green, blue. Row 1: white, (transparent pixel stays), spare.
    palImg.setPixelColor(0, 0, QColor(255, 0, 0, 255));
    palImg.setPixelColor(1, 0, QColor(0, 255, 0, 255));
    palImg.setPixelColor(2, 0, QColor(0, 0, 255, 255));
    palImg.setPixelColor(0, 1, QColor(255, 255, 255, 255));
    // (1,1) is transparent (alpha 0) already.
    check(palImg.save(palPng, "PNG"), "custom-palette: palette PNG saved");

    // Frames whose colors are drawn ONLY from that palette (red/white/blue).
    const QList<QImage> frames = {
        makeSolid(16, 16, QColor(255, 0, 0)),
        makeSolid(16, 16, QColor(0, 0, 255)),
        makeSolid(16, 16, QColor(255, 255, 255)),
    };
    const QString out = QDir::temp().filePath("ae_custom_pal.gif");
    media::GifEncoder g;
    media::GifEncoder::Config c;
    c.outputPath = out; c.fps = 10; c.palettePath = palPng; c.transparent = true;
    QString err;
    check(g.encode(frames, c, &err), "custom-palette: gif encode succeeds");
    check(QFile::exists(out) && QFile(out).size() > 0, "custom-palette: gif file exists + non-empty");
    int w = 0, h = 0, fps = 0;
    const QList<QImage> dec = decodeFrames(out, &w, &h, &fps);
    check(w == 16 && h == 16 && dec.size() == 3, "custom-palette: gif decodes to 3 frames at 16x16");

    // Invalid palette path must fail cleanly, not silently adapt.
    media::GifEncoder g2;
    media::GifEncoder::Config c2;
    c2.outputPath = QDir::temp().filePath("ae_custom_bad.gif");
    c2.fps = 10; c2.palettePath = QDir::temp().filePath("ae_missing_palette.png");
    QString err2;
    check(!g2.encode(frames, c2, &err2), "custom-palette: missing palette path rejected cleanly");
    check(!err2.isEmpty(), "custom-palette: missing palette reports an error");

    QFile::remove(palPng);
    QFile::remove(out);
    return true;
}
// The encoder rounds YUV planes up to even (322x242); the source QImage stays
// 321x241, so the sws pass must NOT read outside the source buffer. Before the
// fix this read one row + one column past the source and could fault on a tight
// allocation. We assert a valid, even-sized, decodable file (and no crash).
static bool run_odd_size_live_encode() {
    const QList<QImage> src = { makeFrame(321, 241, 0), makeFrame(321, 241, 1) };
    const QString out = QDir::temp().filePath("ae_v1_odd.mp4");

    media::VideoEncoder enc;
    media::VideoEncoder::Config cfg;
    cfg.outputPath = out; cfg.container = "mp4"; cfg.codec = "libx264";
    cfg.width = 321; cfg.height = 241; cfg.fps = 24; cfg.pixFmt = "yuv420p";

    bool ok = enc.writeAll(src, cfg);
    check(ok, "odd-size (321x241) encode succeeds (rounded to even, no crash)");

    int w = 0, h = 0, fps = 0;
    const QList<QImage> dec = ok ? decodeFrames(out, &w, &h, &fps) : QList<QImage>();
    check(ok && !dec.isEmpty(), "odd-size encode produces a decodable file");
    check(w % 2 == 0 && h % 2 == 0, "odd-size encode output dimensions are even");
    if (ok && !dec.isEmpty())
        check(dec.first().width() % 2 == 0 && dec.first().height() % 2 == 0,
              "odd-size decode dimensions are even");
    QFile::remove(out);
    return true;
}

// Edge case: alpha channel round-trip. Feed RGBA frames with a known alpha ramp
// through an alpha-capable codec (ffv1 @ yuva420p, which the codec lists) and
// assert the decoded alpha matches the source within a small tolerance (lossless
// alpha plane => exact).
static bool run_alpha_roundtrip() {
    if (!media::ffmpeg::encoderAvailable("ffv1")) {
        QTextStream(stdout) << "SKIP  alpha round-trip (ffv1 absent)\n";
        return true;
    }
    const int w = 64, h = 48;
    QList<QImage> src;
    for (int i = 0; i < 3; ++i)
        src.append(makeFrame(w, h, i));

    const QString out = QDir::temp().filePath("ae_v1_alpha.mkv");
    media::VideoEncoder::Config cfg;
    cfg.outputPath = out; cfg.container = "mkv"; cfg.codec = "ffv1";
    cfg.width = w; cfg.height = h; cfg.fps = 24; cfg.pixFmt = "yuva420p";
    cfg.transparent = true;  // alpha-capable format must be explicitly requested

    media::VideoEncoder enc;
    bool ok = enc.writeAll(src, cfg);
    check(ok, "alpha round-trip encode succeeds (ffv1 @ yuva420p)");

    int dw = 0, dh = 0, dfps = 0;
    const QList<QImage> dec = ok ? decodeFrames(out, &dw, &dh, &dfps) : QList<QImage>();
    check(ok && dec.size() == src.size(), "alpha round-trip decode count matches");

    // Compare the Alpha channel (byte 3) exactly between source and decoded.
    bool alphaExact = true;
    int alphaMismatches = 0;
    int alphaMaxDelta = 0;
    if (dec.size() == src.size()) {
        for (int i = 0; i < src.size(); ++i) {
            const QImage a = src[i].convertToFormat(QImage::Format_RGBA8888);
            const QImage b = dec[i].convertToFormat(QImage::Format_RGBA8888);
            if (a.size() != b.size()) { alphaExact = false; break; }
            for (int y = 0; y < a.height(); ++y) {
                const uchar* sa = a.constScanLine(y);
                const uchar* sb = b.constScanLine(y);
                for (int x = 0; x < a.width(); ++x) {
                    const int d = std::abs(int(sa[4 * x + 3]) - int(sb[4 * x + 3]));
                    if (d != 0) {
                        ++alphaMismatches;
                        alphaMaxDelta = std::max(alphaMaxDelta, d);
                        alphaExact = false;
                    }
                }
            }
        }
    }
    // Alpha must be carried through, not dropped to opaque. The alpha plane is
    // full-resolution and lossless in ffv1, so we assert within a +/-1 tolerance
    // (the residual is sws's fractional rounding on the planar->packed step).
    if (!alphaExact)
        QTextStream(stdout) << "  DEBUG alpha mismatches=" << alphaMismatches
                            << " maxDelta=" << alphaMaxDelta << "\n";
    check(alphaMismatches == 0 || alphaMaxDelta <= 1,
          "alpha round-trip preserves the alpha channel (within +/-1)");
    QFile::remove(out);
    return true;
}

// Edge case: unwritable output path must fail cleanly (no crash, no false success).
static bool run_unwritable_path() {
    media::VideoEncoder enc;
    media::VideoEncoder::Config cfg;
    // A path that points AT an existing directory -> avio_open must fail.
    const QString dirTarget = QDir::temp().filePath("ae_v1_unwritable.mp4");
    QDir().mkpath(dirTarget);                    // make it a DIRECTORY
    cfg.outputPath = dirTarget; cfg.container = "mp4"; cfg.codec = "libx264";
    cfg.width = 64; cfg.height = 64; cfg.fps = 24; cfg.pixFmt = "yuv420p";

    bool ok = enc.open(cfg);
    check(!ok, "unwritable output path rejected (open returns false)");
    check(!enc.error().isEmpty(), "unwritable output path reports an error (no crash)");
    QDir(dirTarget).removeRecursively();          // clean up the directory
    return true;
}

// Edge case: an encoder that exists as a name but has no usable device context
// (HW encoders like h264_nvenc / h264_qsv in a headless env) or an unknown
// encoder must fail gracefully — never a null deref.
static bool run_absent_encoder() {
    // Unknown encoder -> open() must reject cleanly.
    media::VideoEncoder e1;
    media::VideoEncoder::Config c1;
    c1.outputPath = QDir::temp().filePath("ae_v1_nocodec.mp4");
    c1.container = "mp4"; c1.codec = "no_such_encoder_xyz";
    c1.width = 64; c1.height = 64; c1.fps = 24; c1.pixFmt = "yuv420p";
    bool ok1 = e1.open(c1);
    check(!ok1 && !e1.error().isEmpty(), "unknown encoder rejected cleanly (no crash)");

    // A present-but-inert HW encoder name must also fail/not crash.
    for (const char* hw : { "h264_nvenc", "h264_qsv", "av1_nvenc" }) {
        if (!media::ffmpeg::encoderAvailable(hw)) {
            QTextStream(stdout) << "SKIP  HW encoder absent: " << hw << "\n";
            continue;
        }
        media::VideoEncoder e2;
        media::VideoEncoder::Config c2;
        c2.outputPath = QDir::temp().filePath(QString("ae_v1_hw_%1.mp4").arg(hw));
        c2.container = "mp4"; c2.codec = hw;
        c2.width = 128; c2.height = 128; c2.fps = 24; c2.pixFmt = "yuv420p";
        bool ok2 = e2.open(c2);   // no device context in a headless run -> expected false, no crash
        QTextStream(stdout) << (ok2 ? "PASS  " : "INFO  ")
                            << "HW encoder " << hw << " open() -> "
                            << (ok2 ? "succeeded (unexpected, device present?)" : "rejected cleanly") << "\n";
        e2.close();
    }
    return true;
}

// Edge case: zero/negative dimensions + zero fps must be rejected cleanly.
static bool run_zero_negative_size() {
    media::VideoEncoder enc;
    media::VideoEncoder::Config c;
    c.outputPath = QDir::temp().filePath("ae_v1_z.mp4"); c.container = "mp4";
    c.codec = "libx264"; c.width = 0; c.height = 64; c.fps = 24; c.pixFmt = "yuv420p";
    check(!enc.open(c) && !enc.error().isEmpty(), "zero width rejected");

    c.width = -16; c.height = 64;
    check(!enc.open(c) && !enc.error().isEmpty(), "negative width rejected");

    c.width = 64; c.height = 0;
    check(!enc.open(c) && !enc.error().isEmpty(), "zero height rejected");

    c.width = 64; c.height = 64; c.fps = 0;
    check(!enc.open(c) && !enc.error().isEmpty(), "zero fps rejected");
    return true;
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QTextStream(stdout) << "=== V1 media::VideoEncoder suite ===\n";    run_lossless_roundtrip();
    run_mp4_x264();
    run_single_frame();
    run_bad_params();
    run_odd_size_norm();
    run_container_coverage();
    run_video_audio_mux();
    run_make_config();
    run_gif();
    run_custom_palette_gif();
    run_vp9_webm_alpha();
    run_png_anim_alpha();
    run_odd_size_live_encode();
    run_alpha_roundtrip();
    run_unwritable_path();
    run_absent_encoder();
    run_zero_negative_size();
    // A single media::VideoEncoder can be reused across multiple export cycles
    // (open/write/close, then open/write/close again on the same instance). The
    // media layer is independently reusable; ctrl::Exporter's GL-side reuse
    // crash is a separate, pre-existing framebuffer-lifecycle issue. This locks
    // in the media-only half of that guarantee (see the e2e harness comment).
    {
        const QString o1 = QDir::temp().filePath("ae_reuse1.mp4");
        const QString o2 = QDir::temp().filePath("ae_reuse2.mp4");
        media::VideoEncoder enc;
        auto go = [&](const QString& out) {
            media::VideoEncoder::Config cfg;
            cfg.outputPath = out; cfg.container = "mp4"; cfg.codec = "libx264";
            cfg.width = 64; cfg.height = 48; cfg.fps = 24; cfg.pixFmt = "yuv420p";
            QList<QImage> frames = { makeFrame(64, 48, 0), makeFrame(64, 48, 1) };
            return enc.writeAll(frames, cfg);
        };
        bool a = go(o1); QFile::remove(o1);
        bool b = go(o2); QFile::remove(o2);
        check(a && b, "media::VideoEncoder reuse: 2 export cycles on one instance");
    }
    QTextStream(stdout) << "=== result ===\n";
    return failures == 0 ? 0 : 1;
}
