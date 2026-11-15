#include "media/GifEncoder.h"

#include <QImageReader>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

namespace media {

namespace {

constexpr int kPaletteCount = 256;

// Fill any palette slots beyond the supplied colors with grays so the palette
// is always a complete 256 entries (a must for the pal8 GIF encoder).
void fillRemainingGray(QVector<quint32>& aPalette, int aStart) {
    for (int i = aStart; i < kPaletteCount; ++i) {
        const int v = (i - 1) * 255 / (kPaletteCount - 2);
        aPalette[i] = 0xFF000000u | (quint32(v) << 16) | (quint32(v) << 8) | quint32(v);
    }
}

// Build a 256-entry uint32_t palette (0xAARRGGBB) from the frames, adaptive:
// keep the kPaletteCount most frequent opaque colors; if transparency is on,
// entry 0 is reserved as the transparent index.
void buildPalette(const QList<QImage>& aFrames, bool aTransparent,
                  QVector<quint32>& aPalette, bool& aHasTransparent) {
    aHasTransparent = false;
    QHash<quint32, quint32> hist;
    for (const QImage& f : aFrames) {
        QImage img = f.convertToFormat(QImage::Format_RGBA8888);
        for (int y = 0; y < img.height(); ++y) {
            const uchar* line = img.constBits() + y * img.bytesPerLine();
            for (int x = 0; x < img.width(); ++x) {
                const uchar r = line[4 * x + 0], g = line[4 * x + 1], b = line[4 * x + 2], a = line[4 * x + 3];
                if (aTransparent && a < 128) { aHasTransparent = true; continue; }
                // quantize each channel to 5 bits so similar colors merge
                const quint32 key = (quint32(r >> 3) << 10) | (quint32(g >> 3) << 5) | (b >> 3);
                ++hist[key];
            }
        }
    }
    // Convert keys back to representative colors, sort by frequency.
    QVector<QPair<quint32, quint32>> counted;
    counted.reserve(hist.size());
    for (auto it = hist.constBegin(); it != hist.constEnd(); ++it) {
        const quint32 key = it.key();
        const int r = (key >> 10) & 31, g = (key >> 5) & 31, b = key & 31;
        const quint32 color = (quint32((r << 3) | (r >> 2)) << 16) |
                              (quint32((g << 3) | (g >> 2)) << 8) |
                              ((b << 3) | (b >> 2));
        counted.push_back({ (color << 8) | 0xFF, it.value() }); // high byte = alpha (opaque)
    }
    std::sort(counted.begin(), counted.end(),
              [](const auto& x, const auto& y) { return x.second > y.second; });

    aPalette.fill(0, kPaletteCount);
    int paletteIndex = 0;
    if (aTransparent) {
        aPalette[0] = 0x00000000; // transparent index
        paletteIndex = 1;
        aHasTransparent = true;
    }
    for (const auto& c : counted) {
        if (paletteIndex >= kPaletteCount) break;
        aPalette[paletteIndex++] = c.first;
    }
    fillRemainingGray(aPalette, paletteIndex);
}

// Load a user-supplied palette PNG. Its pixels ARE the palette (the ffmpeg
// paletteuse convention): each distinct opaque pixel color becomes one entry.
// If transparency is requested (or the PNG contains a fully-transparent pixel),
// entry 0 is reserved as the transparent index.
bool loadPaletteFromPng(const QString& aPath, bool aTransparent,
                        QVector<quint32>& aPalette, bool& aHasTransparent) {
    QImageReader reader(aPath);
    reader.setAutoTransform(true);
    QImage pal = reader.read();
    if (pal.isNull()) return false; // caller sets a readable-error message

    pal = pal.convertToFormat(QImage::Format_RGBA8888);
    QSet<quint32> seen;
    bool palHasTransparent = false;
    for (int y = 0; y < pal.height(); ++y) {
        const uchar* line = pal.constBits() + y * pal.bytesPerLine();
        for (int x = 0; x < pal.width(); ++x) {
            const uchar r = line[4*x+0], g = line[4*x+1], b = line[4*x+2], a = line[4*x+3];
            if (a < 128) { palHasTransparent = true; continue; }
            const quint32 rgb = (quint32(r) << 16) | (quint32(g) << 8) | quint32(b);
            if (seen.contains(rgb)) continue;
            seen.insert(rgb);
            if (seen.size() > kPaletteCount) break; // too many colors; keep first 256
        }
        if (seen.size() > kPaletteCount) break;
    }

    aPalette.fill(0, kPaletteCount);
    aHasTransparent = aTransparent || palHasTransparent;
    int paletteIndex = 0;
    if (aHasTransparent) {
        aPalette[0] = 0x00000000;
        paletteIndex = 1;
    }
    // Deterministic order (sorted by RGB) so a given PNG always maps identically.
    QList<quint32> colors = seen.values();
    std::sort(colors.begin(), colors.end());
    for (const quint32 rgb : colors) {
        if (paletteIndex >= kPaletteCount) break;
        aPalette[paletteIndex++] = 0xFF000000u | rgb;
    }
    fillRemainingGray(aPalette, paletteIndex);
    return true;
}

// Build a 32x32x32 LUT mapping quantized RGB -> nearest palette index.
void buildLut(const QVector<quint32>& aPalette, QVector<uchar>& aLut) {
    const int kCube = 32;
    aLut.fill(0, kCube * kCube * kCube);
    for (int ci = 0; ci < kCube; ++ci)
        for (int cj = 0; cj < kCube; ++cj)
            for (int ck = 0; ck < kCube; ++ck) {
                const int cell = (ci << 10) | (cj << 5) | ck;
                int best = 1, bestD = INT_MAX;
                // representative color of the cell (5 bits -> 8 bits)
                const int R = (ci << 3) | (ci >> 2);
                const int G = (cj << 3) | (cj >> 2);
                const int B = (ck << 3) | (ck >> 2);
                for (int p = 0; p < aPalette.size(); ++p) {
                    const quint32 c = aPalette[p];
                    if ((c >> 24) == 0) continue; // skip transparent
                    const int pr = (c >> 16) & 0xFF, pg = (c >> 8) & 0xFF, pb = c & 0xFF;
                    const int dr = R - pr, dg = G - pg, db = B - pb;
                    const int d = dr * dr + dg * dg + db * db;
                    if (d < bestD) { bestD = d; best = p; }
                }
                aLut[cell] = uchar(best);
            }
}

} // namespace

bool GifEncoder::encode(const QList<QImage>& aFrames, const Config& aConfig, QString* aError) {
    mError.clear();
    if (aFrames.isEmpty()) { mError = "no frames to encode"; if (aError) *aError = mError; return false; }
    if (aFrames.first().width() <= 0 || aFrames.first().height() <= 0 || aConfig.fps <= 0) {
        mError = "invalid gif parameters"; if (aError) *aError = mError; return false;
    }

    const int width = aFrames.first().width();
    const int height = aFrames.first().height();

    QVector<quint32> palette;
    bool alpha = false;
    if (!aConfig.palettePath.isEmpty()) {
        if (!loadPaletteFromPng(aConfig.palettePath, aConfig.transparent, palette, alpha)) {
            mError = "could not load palette: " + aConfig.palettePath;
            if (aError) *aError = mError;
            return false;
        }
    } else {
        buildPalette(aFrames, aConfig.transparent, palette, alpha);
    }
    QVector<uchar> lut;
    buildLut(palette, lut);

    AVFormatContext* fmt = nullptr;
    if (::avformat_alloc_output_context2(&fmt, nullptr, "gif", aConfig.outputPath.toUtf8().constData()) < 0) {
        mError = "could not allocate gif muxer"; if (aError) *aError = mError; return false;
    }
    const AVCodec* codec = ::avcodec_find_encoder(AV_CODEC_ID_GIF);
    AVCodecContext* ctx = ::avcodec_alloc_context3(codec);
    ctx->width = width; ctx->height = height;
    ctx->pix_fmt = AV_PIX_FMT_PAL8;
    ctx->time_base = AVRational{ 1, aConfig.fps };
    ctx->framerate = AVRational{ aConfig.fps, 1 };
    if (::avcodec_open2(ctx, codec, nullptr) < 0) {
        mError = "could not open gif encoder"; ::avcodec_free_context(&ctx); ::avformat_free_context(fmt);
        if (aError) *aError = mError; return false;
    }
    AVStream* stream = ::avformat_new_stream(fmt, nullptr);
    stream->id = fmt->nb_streams - 1;
    if (::avcodec_parameters_from_context(stream->codecpar, ctx) < 0) {
        mError = "could not set gif stream params"; if (aError) *aError = mError;
        ::avcodec_free_context(&ctx); ::avformat_free_context(fmt); return false;
    }
    stream->time_base = ctx->time_base;
    stream->avg_frame_rate = AVRational{ aConfig.fps, 1 };
    if (::avio_open(&fmt->pb, aConfig.outputPath.toUtf8().constData(), AVIO_FLAG_WRITE) < 0 ||
        ::avformat_write_header(fmt, nullptr) < 0) {
        mError = "could not write gif header"; if (aError) *aError = mError;
        ::avformat_free_context(fmt); ::avcodec_free_context(&ctx); return false;
    }

    bool ok = true;
    AVFrame* frame = ::av_frame_alloc();
    frame->format = AV_PIX_FMT_PAL8;
    frame->width = width; frame->height = height;
    if (::av_frame_get_buffer(frame, 0) < 0) { ok = false; mError = "frame buffer alloc failed"; }
    // The palette lives in data[1] (uint32_t[256]) for pal8.
    if (ok) std::memcpy(frame->data[1], palette.constData(), kPaletteCount * 4);

    for (int fi = 0; fi < aFrames.size() && ok; ++fi) {
        QImage img = aFrames[fi].convertToFormat(QImage::Format_RGBA8888);
        if (::av_frame_make_writable(frame) < 0) { ok = false; mError = "frame not writable"; break; }
        uchar* dst = frame->data[0];
        const int dstLine = frame->linesize[0];
        for (int y = 0; y < height; ++y) {
            const uchar* src = img.constBits() + y * img.bytesPerLine();
            uchar* row = dst + y * dstLine;
            for (int x = 0; x < width; ++x) {
                const uchar r = src[4*x+0], g = src[4*x+1], b = src[4*x+2], a = src[4*x+3];
                if (aConfig.transparent && alpha && a < 128) { row[x] = 0; continue; }
                const int cell = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
                row[x] = lut[cell];
            }
        }
        frame->pts = fi;

        if (::avcodec_send_frame(ctx, frame) >= 0) {
            AVPacket* pkt = ::av_packet_alloc();
            int ret;
            while ((ret = ::avcodec_receive_packet(ctx, pkt)) >= 0) {
                ::av_packet_rescale_ts(pkt, ctx->time_base, stream->time_base);
                pkt->stream_index = stream->index;
                if (::av_interleaved_write_frame(fmt, pkt) < 0) { ok = false; mError = "gif write failed"; }
                ::av_packet_unref(pkt);
                if (!ok) break;
            }
            ::av_packet_free(&pkt);
            if (ok && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) { ok = false; mError = "gif encode error"; }
        } else { ok = false; mError = "gif send_frame failed"; }
    }

    if (ok) {
        ::avcodec_send_frame(ctx, nullptr);
        AVPacket* pkt = ::av_packet_alloc();
        int ret;
        while ((ret = ::avcodec_receive_packet(ctx, pkt)) >= 0) {
            ::av_packet_rescale_ts(pkt, ctx->time_base, stream->time_base);
            pkt->stream_index = stream->index;
            if (::av_interleaved_write_frame(fmt, pkt) < 0) ok = false;
            ::av_packet_unref(pkt);
            if (!ok) break;
        }
        ::av_packet_free(&pkt);
        if (::av_write_trailer(fmt) < 0) ok = false;
    }

    ::av_frame_free(&frame);
    if (fmt->pb) ::avio_closep(&fmt->pb);
    ::avformat_free_context(fmt);
    ::avcodec_free_context(&ctx);

    if (!ok && aError) *aError = mError;
    return ok;
}

} // namespace media
