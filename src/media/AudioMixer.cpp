#include "media/AudioMixer.h"

#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace media {

AudioMixer::AudioMixer() = default;
AudioMixer::~AudioMixer() = default;

qint64 AudioMixer::frameToSamples(int aFrame, int aFps, int aSampleRate) {
    if (aFps <= 0 || aSampleRate <= 0)
        return 0;
    return (qint64(aFrame) * aSampleRate) / aFps;
}

bool AudioMixer::mix(const QVector<Track>& aTracks, const Config& aConfig) {
    mError.clear();
    if (aConfig.fps <= 0) { mError = "invalid fps"; return false; }
    if (aConfig.totalFrames <= 0) { mError = "invalid totalFrames"; return false; }
    if (aConfig.sampleRate <= 0) { mError = "invalid sampleRate"; return false; }
    if (aConfig.channels <= 0) { mError = "invalid channels"; return false; }

    // Output duration in samples per channel = totalFrames / fps seconds.
    const qint64 numSamples = qint64(aConfig.totalFrames) * aConfig.sampleRate / aConfig.fps;
    if (numSamples <= 0) { mError = "zero output duration"; return false; }

    mSampleCount = int(numSamples);
    mChannels = aConfig.channels;
    mSampleRate = aConfig.sampleRate;
    mSamples = QVector<float>(mSampleCount * mChannels, 0.0f);

    int decodeErrors = 0;
    for (const Track& track : aTracks) {
        if (!track.enabled || track.path.isEmpty())
            continue;

        const qint64 offset = frameToSamples(track.startFrame, aConfig.fps, aConfig.sampleRate);
        if (offset < 0)
            continue; // clamp negative start frames: treat as silence (skip)
        if (offset >= numSamples)
            continue; // entirely beyond the export range

        // Clamp to the track's end frame (if set) so it can't overrun.
        qint64 maxCount = numSamples - offset;
        if (track.endFrame > 0) {
            const qint64 endOffset = frameToSamples(track.endFrame, aConfig.fps, aConfig.sampleRate);
            if (endOffset > offset)
                maxCount = std::min(maxCount, endOffset - offset);
            else
                continue; // zero-length (or reversed) contribution
        }
        if (maxCount <= 0)
            continue;

        QVector<float> trackSamples;
        qint64 trackCount = 0;
        QString err;
        if (!decodeTrackToFloats(track.path, aConfig.sampleRate, aConfig.channels,
                                 trackSamples, trackCount, &err)) {
            ++decodeErrors;
            // Keep mixing the rest; a single bad track must not abort the export.
            continue;
        }
        if (trackCount <= 0)
            continue;

        const qint64 toAdd = std::min(trackCount, maxCount);
        const float gain = track.volume;
        float* dst = mSamples.data() + offset * mChannels;
        const float* src = trackSamples.constData();
        for (qint64 i = 0; i < toAdd * mChannels; ++i)
            dst[i] += src[i] * gain;
    }

    // Report decode failures that were skipped. This must be OUTSIDE the loop:
    // a decode failure takes the `continue` path above, which would otherwise
    // skip this assignment when every track fails (leaving error() empty even
    // though tracks were dropped).
    if (decodeErrors > 0)
        mError = QString::number(decodeErrors) + " audio track(s) failed to decode and were skipped";

    // Post-mix clamp to the float sample range so a later s16/aac encoder never
    // sees out-of-range values.
    for (float& s : mSamples) {
        if (s > 1.0f) s = 1.0f;
        else if (s < -1.0f) s = -1.0f;
    }
    return true;
}

bool AudioMixer::decodeTrackToFloats(const QString& aPath, int aOutRate, int aOutChannels,
                                     QVector<float>& aOut, qint64& aOutCount, QString* aErr) const {
    aOut.clear();
    aOutCount = 0;

    AVFormatContext* fmt = nullptr;
    if (::avformat_open_input(&fmt, aPath.toUtf8().constData(), nullptr, nullptr) < 0) {
        if (aErr) *aErr = "could not open " + aPath;
        return false;
    }
    if (::avformat_find_stream_info(fmt, nullptr) < 0) {
        ::avformat_close_input(&fmt);
        if (aErr) *aErr = "no stream info for " + aPath;
        return false;
    }
    const int as = ::av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (as < 0) {
        ::avformat_close_input(&fmt);
        if (aErr) *aErr = "no audio stream in " + aPath;
        return false;
    }

    const AVCodec* dec = ::avcodec_find_decoder(fmt->streams[as]->codecpar->codec_id);
    AVCodecContext* ctx = ::avcodec_alloc_context3(dec);
    ::avcodec_parameters_to_context(ctx, fmt->streams[as]->codecpar);
    if (::avcodec_open2(ctx, dec, nullptr) < 0) {
        ::avcodec_free_context(&ctx);
        ::avformat_close_input(&fmt);
        if (aErr) *aErr = "could not open audio decoder for " + aPath;
        return false;
    }

    // Resample to the target layout/rate as interleaved float.
    AVChannelLayout inLayout{};
    bool haveInLayout = false;
    bool inLayoutAllocated = false; // true only for the mono fallback (free it)
    if (ctx->ch_layout.nb_channels > 0) {
        inLayout = ctx->ch_layout; // borrowed from the decoder context (do not uninit)
        haveInLayout = true;
    } else {
        ::av_channel_layout_default(&inLayout, 1); // unknown -> mono fallback
        haveInLayout = true;
        inLayoutAllocated = true;
    }

    const int inRate = ctx->sample_rate > 0 ? ctx->sample_rate : 48000;
    const AVSampleFormat inFmt = static_cast<AVSampleFormat>(ctx->sample_fmt);

    AVChannelLayout outLayout{};
    ::av_channel_layout_default(&outLayout, aOutChannels);

    SwrContext* swr = nullptr;
    if (::swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, aOutRate,
                              haveInLayout ? &inLayout : nullptr, inFmt, inRate, 0, nullptr) < 0) {
        if (haveInLayout) ::av_channel_layout_uninit(&inLayout);
        ::av_channel_layout_uninit(&outLayout);
        ::avcodec_free_context(&ctx);
        ::avformat_close_input(&fmt);
        if (aErr) *aErr = "could not allocate resampler for " + aPath;
        return false;
    }
    ::av_channel_layout_uninit(&outLayout); // swr has its own copy by now
    if (::swr_init(swr) < 0) {
        ::swr_free(&swr);
        if (haveInLayout) ::av_channel_layout_uninit(&inLayout);
        ::avcodec_free_context(&ctx);
        ::avformat_close_input(&fmt);
        if (aErr) *aErr = "could not init resampler for " + aPath;
        return false;
    }

    AVFrame* frame = ::av_frame_alloc();
    AVPacket* pkt = ::av_packet_alloc();
    QVector<float> out; // interleaved

    auto convert = [&](const AVFrame* f, int inCount) -> bool {
        const int cap = ::swr_get_out_samples(swr, inCount);
        if (cap < 0) return false;
        if (cap > 0) {
            const qsizetype prev = out.size();
            out.resize(prev + qsizetype(cap) * aOutChannels);
            uint8_t* outArr[1] = { reinterpret_cast<uint8_t*>(out.data() + prev) };
            const int n = ::swr_convert(swr, outArr, cap,
                                        f ? const_cast<const uint8_t* const*>(f->extended_data) : nullptr,
                                        inCount);
            if (n < 0) return false;
            out.resize(prev + qsizetype(n) * aOutChannels);
        }
        return true;
    };

    bool ok = true;
    while (::av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == as) {
            if (::avcodec_send_packet(ctx, pkt) >= 0) {
                while (::avcodec_receive_frame(ctx, frame) >= 0) {
                    if (!convert(frame, frame->nb_samples)) { ok = false; break; }
                }
            }
        }
        ::av_packet_unref(pkt);
        if (!ok) break;
    }
    if (ok) {
        ::avcodec_send_packet(ctx, nullptr);
        while (::avcodec_receive_frame(ctx, frame) >= 0) {
            if (!convert(frame, frame->nb_samples)) { ok = false; break; }
        }
        if (ok && !convert(nullptr, 0)) ok = false; // flush the resampler
    }

    ::av_frame_free(&frame);
    ::av_packet_free(&pkt);
    ::swr_free(&swr);
    if (inLayoutAllocated)
        ::av_channel_layout_uninit(&inLayout);
    ::avcodec_free_context(&ctx);
    ::avformat_close_input(&fmt);

    if (!ok) {
        if (aErr) *aErr = "audio decode/resample failed for " + aPath;
        return false;
    }

    const int ch = aOutChannels;
    if (out.size() % ch != 0)
        out.resize(out.size() - (out.size() % ch)); // drop a partial interleaved frame
    aOut = std::move(out);
    aOutCount = aOut.size() / ch;
    return true;
}

} // namespace media
