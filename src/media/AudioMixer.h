#ifndef ANIMEEFFECTS_SRC_MEDIA_AUDIOMIXER_H
#define ANIMEEFFECTS_SRC_MEDIA_AUDIOMIXER_H

#include <QVector>
#include <QString>

// In-process multi-track audio mixer over the linked libav* (avformat +
// avcodec + swresample). This is the library-side replacement for the removed
// per-track `ffmpeg` CLI remux chain: it decodes each enabled track, resamples
// it to a common rate/channel layout, offsets it by the track's start frame,
// applies its volume, and SUMS all tracks into one interleaved float buffer.
//
// GL-free and QCoreApplication-free, so it is unit-testable headless.
namespace media {

class AudioMixer {
public:
    struct Track {
        QString path;       // audio file (any format libav can demux/decode)
        int startFrame = 0; // offset, in project frames (clamp to 0 if negative)
        int endFrame = 0;   // 0 = to the end of the export range; else clamp
        float volume = 1.0f; // linear gain applied before mixing
        bool enabled = true;
    };

    struct Config {
        int fps = 24;        // project frame rate (timebase)
        int totalFrames = 0; // export duration in frames; tracks clamp to this
        int sampleRate = 48000;
        int channels = 2;    // output channel count
    };

    AudioMixer();
    ~AudioMixer();
    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    // Mix all enabled tracks into the internal interleaved float buffer.
    // Returns false (with error()) if any field is invalid or a decode fails
    // fatally. Tracks that fail to decode are skipped (with a best-effort).
    bool mix(const QVector<Track>& aTracks, const Config& aConfig);

    // Interleaved float samples (channels interleaved). Ownership stays here.
    const float* samples() const { return mSamples.constData(); }
    int sampleCount() const { return mSampleCount; } // per channel
    int channels() const { return mChannels; }
    int sampleRate() const { return mSampleRate; }
    bool isSilent() const { return mSampleCount <= 0; }

    QString error() const { return mError; }

    // Convert a project frame to an output sample index (frame * sampleRate / fps).
    static qint64 frameToSamples(int aFrame, int aFps, int aSampleRate);

private:
    bool decodeTrackToFloats(const QString& aPath, int aOutRate, int aOutChannels,
                             QVector<float>& aOut, qint64& aOutCount,
                             QString* aErr) const;

    QVector<float> mSamples; // interleaved
    int mSampleCount = 0;
    int mChannels = 0;
    int mSampleRate = 0;
    QString mError;
};

} // namespace media

#endif
