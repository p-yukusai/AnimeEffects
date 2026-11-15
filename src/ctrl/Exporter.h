#ifndef CTRL_EXPORTER_H
#define CTRL_EXPORTER_H

#include <list>
#include <memory>
#include <functional>
#include <QString>
#include <QSize>
#include <QList>
#include <QMap>
#include <QImage>
#include <QFileInfo>
#include <QOpenGLFramebufferObject>
#include "util/Range.h"
#include "ctrl/ExportTarget.h"   // FrameSelection
#include "util/IProgressReporter.h"
#include "ctrl/UILogger.h"
#include "gl/EasyTextureDrawer.h"
#include "gl/VertexArrayObject.h"
#include "core/Project.h"
#include "core/TimeInfo.h"
#include "core/TimeKeyBlender.h"
#include "core/ClippingFrame.h"
#include "core/FilterFrame.h"
#include "core/DestinationTexturizer.h"
#include "media/VideoEncoder.h"
#include "media/AudioMixer.h"

namespace ctrl {

class Exporter {
public:
    typedef std::function<bool(const QString&)> OverwriteConfirmer;

    // Render the selection and hand the FRAMES to this instead of encoding a file.
    //
    // Fitting a file under a destination's hard byte cap means encoding,
    // measuring, and encoding again at a lower quality — so the caller needs the
    // frames, not a finished file. The frames arrive at the RENDER size, which is
    // what media::VideoEncoder::Config::fit expects (it pads or crops to the
    // output size itself); padding them here instead would double-fit an
    // exact-size destination.
    //
    // Called once, from finish(), after every frame has been rendered.
    typedef std::function<void(const QList<QImage>&)> FrameSink;

    enum ResultCode {
        ResultCode_Success,
        ResultCode_Canceled,
        ResultCode_InvalidOperation,
        ResultCode_EncoderOpenFailed,   // encoder could not be opened for the target/codec
        ResultCode_EncodeError,         // write/encode/mux error mid-export
        ResultCode_UnclassfiedError,    // (kept as-is; original spelling)
        ResultCode_TERM
    };

    struct Result {
        Result();
        Result(ResultCode aCode, const QString& aMessage);
        explicit operator bool() const { return code == ResultCode_Success; }
        ResultCode code;
        QString message;
    };

    struct CommonParam {
        CommonParam();
        QString path;
        QSize size;
        // WHICH frames to render, in output order. Several ranges are rendered
        // back to back and the gaps are NOT exported. This was a single
        // util::Range, which is why several ranges collapsed into one span and
        // frames the user had excluded were written anyway. A single contiguous
        // export is just FrameSelection::fromRange(min, max).
        FrameSelection selection;
        int fps;
        // What to RENDER at, when the destination demands an exact output size
        // that the project's aspect does not match. Empty means "same as size"
        // (the historical behaviour). Destinations that require an exact size
        // (Discord 320x320, WhatsApp 512x512) set this to the fitted size and
        // let `fit` reconcile the remainder, so art is never stretched.
        QSize renderSize;
        media::FitMode fit = media::FitMode::None;
        bool isValid() const;
    };

    struct VideoParam {
        VideoParam();
        // The container (muxer) to write into: "mp4", "webm", "mkv", "mov",
        // "gif", ... Empty means mp4. A NAME, not an index into a preset table:
        // this used to be a ctrl::VideoFormat carrying a whole codec list read
        // from a text asset, of which the exporter ever used two fields.
        QString container;
        int colorIndex;    // colorspace box index (set by the quick-export dialog); exporter
                           // does not apply colorspace yet (see the UI follow-up)
        int bps;           // bitrate in kbps (exporter converts to bits/sec)
        QString pixfmt;    // e.g. "yuv420p"; empty = per-codec auto
        QString codec;     // explicit encoder (friendly or libav name); empty = Auto/default
        bool transparent;  // honor alpha when the codec/container allow it
        // Private encoder options handed to avcodec_open2 (e.g. libvpx needs
        // auto-alt-ref=0 for VP9 alpha, or alt-ref frames corrupt the alpha
        // plane). The destination table carries these; without them the table's
        // documented gotchas could not reach the encoder at all.
        QMap<QString, QString> encOptions;
        // Four-char AVStream codec tag (e.g. hvc1, without which Safari refuses
        // HEVC). A container concern, deliberately not an encoder option.
        QString codecTag;
        // Audio parameters, when a destination specifies them (a source upload
        // wants generous bitrate, a website cares about bandwidth, an archive
        // wants lossless). Empty/zero falls back to the container-derived
        // default, which is what the exporter used unconditionally before: it
        // hardcoded aac-at-128k for everything that was not WebM, so the table's
        // per-destination audio could not take effect.
        QString audioCodec;
        int audioBitrate = 0;
        int audioChannels = 0;
        int audioSampleRate = 0;
    };

    struct GifParam {
        GifParam();
        // NOTE: the legacy `optimizePalette` / `intermediateBps` fields are gone.
        // They addressed the ffmpeg CLI subprocess (its palettegen pass and the
        // intermediate bitrate of a two-pass encode); media::GifEncoder has no
        // pass structure to configure, so both were written by the caller and read
        // by nothing. A parameter that cannot affect the output is not a setting.
        QString palettePath;    // optional PNG palette (pixels are the colors); empty = adaptive
        bool transparent = false; // honor alpha (reserve a transparent palette entry)
    };

    struct ImageParam {
        ImageParam();
        QString name;
        QString suffix;
        int quality;
        // ONE image file at `name`.`suffix`, not a numbered sequence. A still image
        // destination (posting an illustration) promises the file the user named in
        // the save dialog; writing `name0001.png` beside it instead would honour the
        // letter of "image export" and not the request.
        bool singleFile = false;
    };

    Exporter(core::Project& aProject);
    ~Exporter();

    void setOverwriteConfirmer(const OverwriteConfirmer& aConfirmer);
    void setProgressReporter(util::IProgressReporter& aReporter);
    void setFrameSink(FrameSink aSink) { mFrameSink = std::move(aSink); }
    void setUILogger(ctrl::UILogger& aLogger);

    // Supply the multi-track audio to mix and mux alongside the video. Call
    // before execute(); ignored for image/GIF exports. The tracks are in
    // PROJECT frame coordinates; they are shifted into the export window (the
    // exported frame 0 == the export range's minimum frame) at mix time.
    // sampleRate/channels are the mix target (e.g. 48000 stereo).
    void setAudioTracks(const QVector<media::AudioMixer::Track>& aTracks,
                        int aSampleRate = 48000, int aChannels = 2);
    bool hasAudio() const { return mHasAudio; }

    Result execute(const CommonParam& aCommon, const ImageParam& aImage);
    Result execute(const CommonParam& aCommon, const GifParam& aGif);
    Result execute(const CommonParam& aCommon, const VideoParam& aVideo);

    const QString& log() const { return mLog; }
    bool isCanceled() const { return mIsCanceled; }

private:
    typedef std::unique_ptr<QOpenGLFramebufferObject> FramebufferPtr;
    typedef std::list<FramebufferPtr> FramebufferList;

    int mTick = 0;
    Result execute();
    Result start();
    bool update();
    Result finish();
    bool updateTime(core::TimeInfo& aDst);
    bool exportImage(const QImage& aFboImage, int aIndex);
    void destroyFramebuffers();
    void releaseExportVAO();
    void createFramebuffers(const QSize& aOriginSize, const QSize& aExportSize);
    void setTextureParam(QOpenGLFramebufferObject& aFbo);
    static int getDigitCount(int aFrameCount, int aFps, int aFpsOrigin);
    bool decideImagePath(int aIndex, QFileInfo& aPath);
    bool checkOverwriting(const QFileInfo& aPath);
    void updateLog();

    core::Project& mProject;
    FramebufferList mFramebuffers;
    // A Core profile requires a bound vertex array object for EVERY draw, and the
    // transform-feedback pass that poses each mesh is a draw like any other. The
    // canvas happens to keep one bound for its own lifetime, which is why an
    // export launched from the running app worked — and why every headless caller
    // (the test harness, anything that renders without a widget) silently wrote
    // blank files at 0% ink with no GL error to show for it.
    //
    // An exporter must not depend on ambient GL state it does not own, so it owns
    // this one and gives the previous binding back when it is done: leaving its
    // own VAO bound after deleting it would break the canvas that painted before.
    QScopedPointer<gl::VertexArrayObject> mExportVAO;
    GLint mPreviousVAO = 0;

    // Frame-sink mode: frames are collected and handed over instead of encoded.
    FrameSink mFrameSink;
    QList<QImage> mSinkFrames;

    QScopedPointer<core::ClippingFrame> mClippingFrame;
    QScopedPointer<core::FilterFrame> mFilterFrame;
    QScopedPointer<core::DestinationTexturizer> mDestinationTexturizer;
    gl::EasyTextureDrawer mTextureDrawer;
    core::TimeInfo mOriginTimeInfo;
    OverwriteConfirmer mOverwriteConfirmer;
    bool mOverwriteConfirmation;
    util::IProgressReporter* mProgressReporter;
    ctrl::UILogger* mUILogger;

    CommonParam mCommonParam;
    // The selection flattened to project frame numbers, in output order, built
    // once in start(). updateTime() indexes this instead of assuming min()+n.
    QList<int> mSelectedFrames;
    ImageParam mImageParam;
    bool mVideoExporting;
    // GIF path: collect rendered frames (no live encoder) and encode with the
    // in-process media::GifEncoder (replaces the legacy CLI palette subprocess).
    bool mCollectFrames = false;
    QList<QImage> mGifFrames;
    QString mGifPalettePath;   // optional custom palette PNG (empty = adaptive)
    bool mGifTransparent = false; // honor alpha on GIF (reserve a transparent entry)

    media::VideoEncoder mVideoEncoder;
    QVector<media::AudioMixer::Track> mAudioTracks;    int mAudioSampleRate = 48000;
    int mAudioChannels = 2;
    bool mHasAudio = false;
    bool mExporting;
    int mIndex;
    int mDigitCount;
    float mProgress;
    QString mLog;
    bool mIsCanceled;
};

} // namespace ctrl

#endif // CTRL_EXPORTER_H
