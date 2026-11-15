#ifndef ANIMEEFFECTS_SRC_MEDIA_VIDEOENCODER_H
#define ANIMEEFFECTS_SRC_MEDIA_VIDEOENCODER_H

#include <QString>
#include <QSize>
#include <QImage>
#include <QMap>

// In-process video encoder on top of the linked libav* (avformat + avcodec +
// swscale). This is the library-side replacement for the removed `ffmpeg` CLI
// subprocess: it consumes QImage frames and writes a single video stream to a
// container, using the encoder the caller names (libx264/libx265/mpeg4/vp9/
// prores/utvideo/huffyuv/ffv1/...).
//
// GL-free and QCoreApplication-free, so it is unit-testable headless.
namespace media {

// What to do when a source frame's aspect ratio differs from the output size.
// Pad never loses content but leaves empty (transparent) margins; Crop fills
// the frame but can cut off the subject. Which is right depends on the art and
// the destination, so it is a choice rather than a hardcoded behaviour.
enum class FitMode {
    None, // scale to exactly the output size (distorts if the aspect differs)
    Pad,  // fit inside, centre, fill the remainder with transparency
    Crop  // fill the frame, centre, discard the overflow
};


// Reconcile a frame's aspect ratio with a required output size: Pad fits the art
// inside and fills the remainder with transparency, Crop fills the frame and
// discards the overflow. Shared by the encoder (which pads a rendered frame) and
// by callers that must produce an exactly-sized image themselves (e.g. the GIF
// collector) — one implementation, so the two cannot drift.
QImage fitImage(const QImage& aImage, const QSize& aTarget, FitMode aFit);

class VideoEncoder {
public:
    struct Config {
        QString outputPath;    // destination file (extension selects the muxer)
        QString container;     // "mp4","webm","mov","avi","ogv","matroska","gif" ("" => guess from outputPath)
        QString codec;         // libx264, libx265, mpeg4, libvpx-vp9, libvpx, prores_ks, utvideo, ffv1, huffyuv, theora, ...
        int width = 0;
        int height = 0;
        int fps = 0;           // must be > 0
        int bitrate = 0;       // bits/sec; 0 = let the encoder choose
        QString pixFmt;        // requested target ("yuv420p","yuva420p","rgba","yuv444p"); "" = auto per codec
        // How to reconcile a differing aspect ratio. Destinations that demand
        // an exact size (Discord 320x320, WhatsApp 512x512) set Pad or Crop;
        // None distorts and exists only for callers that have already matched
        // the aspect themselves.
        FitMode fit = FitMode::None;

        bool transparent = false; // hint to prefer an alpha-capable pixel format
        int quality = -1;      // optional encoder quality hint (e.g. libx264 -crf); -1 = default

        // Private encoder options, passed to avcodec_open2 (e.g. libvpx's
        // "auto-alt-ref"=0, which VP9+alpha REQUIRES — alt-ref frames corrupt
        // the alpha plane). These are codec-specific by nature, so they are a
        // map rather than typed fields. An unrecognised key is an ERROR, not a
        // silently-ignored typo: a misspelt option here means broken output
        // with no other symptom.
        QMap<QString, QString> encOptions;

        // Muxer options, passed to avformat_write_header (e.g. movflags=+faststart,
        // which puts the moov atom at the front of an MP4). Deliberately separate
        // from encOptions: these configure the CONTAINER, not the encoder, and a
        // platform that demands faststart rejects the file without it. An
        // unrecognised key is an ERROR for the same reason an unknown encoder
        // option is: a silently-ignored requirement is a rejected upload.
        QMap<QString, QString> muxerOptions;

        // FourCC stream codec tag (e.g. "hvc1"). This is NOT an encoder option
        // — it is written into the container's stream entry, and for HEVC it is
        // the difference between Safari playing the file and refusing it
        // (untagged hev1 is rejected). Deliberately separate from encOptions.
        QString codecTag;

        // Optional audio stream (muxed alongside video in one pass).
        // Set audioChannels > 0 + audioSampleRate > 0 + audioCodec to enable.
        QString audioCodec;    // "aac" (mp4/mov/mkv), "libopus" (webm), ...
        int audioChannels = 0;
        int audioSampleRate = 0;
        int audioBitrate = 128000;
    };

    VideoEncoder();
    ~VideoEncoder();

    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    bool open(const Config& aConfig);
    bool write(const QImage& aFrame); // returns false on error; caller should stop
    bool close();                     // flush encoder + write trailer; returns true if the file is valid

    // Provide the pre-mixed interleaved float audio (from AudioMixer) to mux as
    // one audio stream on close(). Channels/rate must match the Config. Must be
    // called after open(), before close(). Returns false on parameter mismatch.
    bool setAudio(const float* aInterleaved, int aChannels, int aSampleRate,
                  qint64 aSampleCountPerChannel);
    bool hasAudio() const;

    // Convenience: open -> write each frame in order -> close. Returns false if
    // any step failed (error() gives the reason).
    bool writeAll(const QList<QImage>& aFrames, const Config& aConfig);

    QString error() const { return mError; }
    int writtenFrames() const { return mWrittenFrames; }

    // Even-nudge for YUV planar sizes (yuv420p requires even dimensions).
    static QSize normalizedSize(const QSize& aSize, const QString& aPixFmt);

    // Build a validated Config from export-preset fields (no encoding). Resolves
    // the encoder name (friendly -> libav, with aliases), normalizes the
    // container ("mkv" -> "matroska"), converts bitrate (kbps) to bits/sec, and
    // picks a pixel format the encoder supports (honoring the transparent hint).
    // On failure (e.g. encoder absent in the linked build) the returned Config
    // has an empty codec and *aError, if given, is set.
    static Config makeConfig(const QString& aContainer, const QString& aCodec,
                             const QString& aPixFmt, bool aTransparent,
                             int aWidth, int aHeight, int aFps, int aBitrateKbps,
                             QString* aError = nullptr);

private:
    bool openMuxer();
    bool openCodec();
    bool allocInputFrame();
    bool writeFrame(const QImage& aFrame);
    bool drainEncoder();
    bool openAudio(const Config& aConfig); // set up the audio stream + codec at open()
    bool writeAudio();                     // encode + mux the buffered audio at close()
    void cleanup();

    struct Impl; // opaque pimpl keeps the libav* types out of this header
    Impl* mImpl;

    QString mError;
    int mWrittenFrames = 0;
    bool mOpened = false;
};

} // namespace media

#endif
