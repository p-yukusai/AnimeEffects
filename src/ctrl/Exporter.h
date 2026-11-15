#ifndef CTRL_EXPORTER_H
#define CTRL_EXPORTER_H

#include <list>
#include <memory>
#include <functional>
#include <QString>
#include <QSize>
#include <QList>
#include <QImage>
#include <QFileInfo>
#include <QOpenGLFramebufferObject>
#include "util/Range.h"
#include "util/IProgressReporter.h"
#include "ctrl/UILogger.h"
#include "gl/EasyTextureDrawer.h"
#include "core/Project.h"
#include "core/TimeInfo.h"
#include "core/TimeKeyBlender.h"
#include "core/ClippingFrame.h"
#include "core/FilterFrame.h"
#include "core/DestinationTexturizer.h"
#include "ctrl/VideoFormat.h"
#include "media/VideoEncoder.h"
#include "media/AudioMixer.h"

namespace ctrl {

class Exporter {
public:
    typedef std::function<bool(const QString&)> OverwriteConfirmer;

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
        util::Range frame;
        int fps;
        bool isValid() const;
    };

    struct VideoParam {
        VideoParam();
        VideoFormat format;
        int codecIndex;    // index into format.codecs, or -1
        int colorIndex;    // colorspace box index (set by the quick-export dialog); exporter
                           // does not apply colorspace yet (see the UI follow-up)
        int bps;           // bitrate in kbps (exporter converts to bits/sec)
        QString pixfmt;    // e.g. "yuv420p"; empty = per-codec auto
        QString codec;     // explicit encoder (friendly or libav name); empty = Auto/default
        bool transparent;  // honor alpha when the codec/container allow it
    };

    struct GifParam {
        GifParam();
        bool optimizePalette;   // legacy no-op (the in-process encoder always builds a palette)
        int intermediateBps;    // legacy no-op
        QString palettePath;    // optional PNG palette (pixels are the colors); empty = adaptive
        bool transparent = false; // honor alpha (reserve a transparent palette entry)
    };

    struct ImageParam {
        ImageParam();
        QString name;
        QString suffix;
        int quality;
    };

    Exporter(core::Project& aProject);
    ~Exporter();

    void setOverwriteConfirmer(const OverwriteConfirmer& aConfirmer);
    void setProgressReporter(util::IProgressReporter& aReporter);
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
    void createFramebuffers(const QSize& aOriginSize, const QSize& aExportSize);
    void setTextureParam(QOpenGLFramebufferObject& aFbo);
    static int getDigitCount(const util::Range& aRange, int aFps, int aFpsOrigin);
    bool decideImagePath(int aIndex, QFileInfo& aPath);
    bool checkOverwriting(const QFileInfo& aPath);
    void updateLog();

    core::Project& mProject;
    FramebufferList mFramebuffers;
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
    ImageParam mImageParam;
    const char* mVideoInCodec;
    int mVideoInCodecQuality;
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
