#include <QFileInfo>
#include <QBuffer>
#include <QApplication>
#include "util/SelectArgs.h"
#include "gl/Global.h"
#include "gl/Util.h"
#include "ctrl/Exporter.h"
#include "media/FFmpegHelpers.h"
#include "media/GifEncoder.h"

namespace ctrl {
//-------------------------------------------------------------------------------------------------
Exporter::Result::Result(): code(ResultCode_TERM) {}

Exporter::Result::Result(ResultCode aCode, const QString& aMessage): code(aCode), message(aMessage) {}

//-------------------------------------------------------------------------------------------------
Exporter::CommonParam::CommonParam(): path(), size(), frame(), fps() {}

bool Exporter::CommonParam::isValid() const {
    if (frame.diff() < 0) {
        return false;
    }

    if (fps <= 0) {
        return false;
    }

    if (size.width() <= 0 || size.height() <= 0) {
        return false;
    }

    return true;
}

//-------------------------------------------------------------------------------------------------
Exporter::ImageParam::ImageParam(): name(), suffix("png"), quality(-1) {}

//-------------------------------------------------------------------------------------------------
Exporter::GifParam::GifParam(): optimizePalette(), intermediateBps() {
    // palettePath defaults to empty (adaptive palette).
}

//-------------------------------------------------------------------------------------------------
Exporter::VideoParam::VideoParam(): format(), codecIndex(-1), colorIndex(), bps(), pixfmt(), codec(), transparent(false) {}

//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
Exporter::Exporter(core::Project& aProject):
    mProject(aProject),
    mFramebuffers(),
    mClippingFrame(),
    mDestinationTexturizer(),
    mTextureDrawer(),
    mOriginTimeInfo(),
    mOverwriteConfirmer(),
    mOverwriteConfirmation(),
    mProgressReporter(),
    mUILogger(),
    mCommonParam(),
    mImageParam(),
    mVideoInCodec(),
    mVideoInCodecQuality(),
    mVideoExporting(),
    mExporting(false),
    mIndex(0),
    mDigitCount(0),
    mProgress(0.0f),
    mLog(),
    mIsCanceled() {}

Exporter::~Exporter() {
    finish();

    // kill buffer
    gl::Global::makeCurrent();
    destroyFramebuffers();
}

void Exporter::setOverwriteConfirmer(const OverwriteConfirmer& aConfirmer) { mOverwriteConfirmer = aConfirmer; }

void Exporter::setProgressReporter(util::IProgressReporter& aReporter) { mProgressReporter = &aReporter; }

void Exporter::setUILogger(UILogger& aLogger) { mUILogger = &aLogger; }

void Exporter::setAudioTracks(const QVector<media::AudioMixer::Track>& aTracks,
                              int aSampleRate, int aChannels) {
    mAudioTracks = aTracks;
    mAudioSampleRate = aSampleRate > 0 ? aSampleRate : 48000;
    mAudioChannels = aChannels > 0 ? aChannels : 2;
    mHasAudio = !mAudioTracks.isEmpty();
}

Exporter::Result Exporter::execute(const CommonParam& aCommon, const ImageParam& aImage) {
    // check param
    if (!aCommon.isValid()) {
        mLog = "Invalid common parameters.";
        return Result(ResultCode_InvalidOperation, mLog);
    }

    // check directory
    QFileInfo path(aCommon.path);
    if (!path.exists() || !path.isDir()) {
        mLog = "Invalid directory path.";
        return Result(ResultCode_InvalidOperation, mLog);
    }

    mCommonParam = aCommon;
    mImageParam = aImage;
    mVideoExporting = false;
    mOriginTimeInfo = mProject.currentTimeInfo();
    mOverwriteConfirmation = false;
    mLog.clear();
    mIsCanceled = false;

    return execute();
}

Exporter::Result Exporter::execute(const CommonParam& aCommon, const GifParam& aGif) {
    // check param
    if (!aCommon.isValid()) {
        mLog = "Invalid common parameters.";
        return Result(ResultCode_InvalidOperation, mLog);
    }

    // check file overwriting
    mOverwriteConfirmation = false;
    QFileInfo filePath(aCommon.path);
    if (!checkOverwriting(filePath)) {
        mLog = "Exporting was canceled.";
        mIsCanceled = true;
        return Result(ResultCode_Canceled, mLog);
    }

    mCommonParam = aCommon;
    mOriginTimeInfo = mProject.currentTimeInfo();
    mLog.clear();
    mIsCanceled = false;

    // In-process GIF: render every frame into a list, then encode + mux a .gif
    // with media::GifEncoder (the legacy ffmpeg CLI palettegen/paletteuse
    // subprocess is gone). optimizePalette/intermediateBps are no longer used.
    // A user-supplied palettePath (a PNG whose pixels are the palette) is fed
    // through to the encoder; otherwise it builds an adaptive palette.
    mGifPalettePath = aGif.palettePath;
    mGifTransparent = aGif.transparent;
    mCollectFrames = true;
    mGifFrames.clear();
    return execute();
}

Exporter::Result Exporter::execute(const CommonParam& aCommon, const VideoParam& aVideo) {
    // check param
    if (!aCommon.isValid()) {
        mLog = "Invalid common parameters.";
        return Result(ResultCode_InvalidOperation, mLog);
    }

    // check file overwriting
    mOverwriteConfirmation = false;
    QFileInfo filePath(aCommon.path);
    if (!checkOverwriting(filePath)) {
        mLog = "Exporting was canceled.";
        mIsCanceled = true;
        return Result(ResultCode_Canceled, mLog);
    }

    mCommonParam = aCommon;
    mVideoExporting = true;
    mOriginTimeInfo = mProject.currentTimeInfo();
    mLog.clear();
    mIsCanceled = false;

    {
        const QString outPath = filePath.absoluteFilePath();

        VideoCodec videoCodec;
        if (aVideo.codecIndex != -1 && !aVideo.format.codecs.isEmpty())
            videoCodec = aVideo.format.codecs.at(aVideo.codecIndex);

        // Codec: explicit override > preset codec > a muxer-aware container
        // default. The explicit override lets the dialog's per-container encoder
        // choice be honored instead of always falling back to the container
        // default. (command/colorspace/icodec are CLI-subprocess artifacts; the
        // in-process encoder consumes only name/pixfmt/transparent.)
        QString codecName = aVideo.codec;
        if (codecName.isEmpty() && !videoCodec.name.isEmpty())
            codecName = videoCodec.name;
        if (codecName.isEmpty()) {
            codecName = media::ffmpeg::defaultCodecForContainer(aVideo.format.name);
            if (codecName.isEmpty()) {
                mLog = "No available video encoder can carry the '" + aVideo.format.name + "' container.";
                return Result(ResultCode_EncoderOpenFailed, mLog);
            }
        }

        // Resolve friendly names (e.g. "h264" -> "libx264") and verify the muxer
        // can actually hold this codec before we open the encoder. This turns a
        // container/codec mismatch into a clean rejection instead of a confusing
        // muxer failure at write time.
        codecName = media::ffmpeg::resolveEncoderName(codecName);
        if (codecName.isEmpty()) {
            mLog = "Encoder not available in this build: " + aVideo.codec;
            return Result(ResultCode_EncoderOpenFailed, mLog);
        }
        if (!media::ffmpeg::muxerSupportsCodec(aVideo.format.name, codecName)) {
            mLog = "The '" + aVideo.format.name + "' container cannot carry the " + codecName + " video codec.";
            return Result(ResultCode_EncoderOpenFailed, mLog);
        }

        QString encoderError;
        media::VideoEncoder::Config cfg = media::VideoEncoder::makeConfig(
            aVideo.format.name, codecName, aVideo.pixfmt, videoCodec.transparent || aVideo.transparent,
            mCommonParam.size.width(), mCommonParam.size.height(), mCommonParam.fps,
            aVideo.bps, &encoderError);

        if (cfg.codec.isEmpty()) {
            mLog = "Video encoder error.\n" + encoderError;
            return Result(ResultCode_EncoderOpenFailed, mLog);
        }
        cfg.outputPath = outPath;

        // Configure the audio stream (muxed in the same pass) when tracks were
        // supplied AND the container supports a standard audio codec we have.
        // This keeps AVI/Ogg exports video-only rather than failing on an
        // audio codec the container can't carry.
        if (mHasAudio) {
            const QString container = aVideo.format.name;
            static const QStringList audioContainers = {
                QStringLiteral("mp4"), QStringLiteral("mov"),
                QStringLiteral("webm"), QStringLiteral("mkv"), QStringLiteral("matroska"),
            };
            if (audioContainers.contains(container)) {
                const QString audioCodec = container == QLatin1String("webm")
                                               ? QStringLiteral("libopus")
                                               : QStringLiteral("aac");
                if (media::ffmpeg::encoderAvailable(audioCodec.toUtf8().constData())) {
                    cfg.audioCodec = audioCodec;
                    cfg.audioChannels = mAudioChannels;
                    cfg.audioSampleRate = mAudioSampleRate;
                    cfg.audioBitrate = 128000;
                }
            }
        }

        if (!mVideoEncoder.open(cfg)) {
            mLog = "Video encoder error.\n" + mVideoEncoder.error();
            return Result(ResultCode_EncoderOpenFailed, mLog);
        }
    }

    return execute();
}

Exporter::Result Exporter::execute() {
    if (mProgressReporter) {
        mProgressReporter->setSection("Exporting...");
        mProgressReporter->setMaximum(100);
    }

    // start
    {
        auto result = start();
        if (!result)
            return result;
    }

    while (true) {
        if (!update())
            break;

        if (mProgressReporter) {
            mProgressReporter->setProgress((int)(100 * mProgress));
            QString sectionName = "";
            if (98.5f <= (float)100 * mProgress) {
                sectionName = QCoreApplication::translate("Exporter", "Finishing up");
            } else {
                sectionName = QCoreApplication::translate("Exporter", "Rendering");
            }

            int tickRate = 4;
            sectionName += QStringLiteral(".").repeated(qFloor(mTick / tickRate) + 1);

            mTick += 1;
            if (mTick >= 3 * tickRate) {
                mTick = 0;
            }

            mProgressReporter->setSection(sectionName);

            if (mProgressReporter->wasCanceled()) {
                finish();
                mLog = "Export was canceled.";
                mIsCanceled = true;
                return Result(ResultCode_Canceled, mLog);
            }
        }
    }

    return finish();
}

Exporter::Result Exporter::start() {
    // reset value
    mIndex = 0;
    mProgress = 0.0f;
    mDigitCount = getDigitCount(mCommonParam.frame, mCommonParam.fps, mOriginTimeInfo.fps);

    // initialize graphics
    {
        gl::Global::makeCurrent();

        // texture drawer
        if (!mTextureDrawer.init()) {
            mLog = "Failed to initialize TextureDrawer";
            Result(ResultCode_UnclassfiedError, mLog);
        }

        // framebuffers
        createFramebuffers(mProject.attribute().imageSize(), mCommonParam.size);

        // clipping frame
        mClippingFrame.reset(new core::ClippingFrame());
        mClippingFrame->resize(mProject.attribute().imageSize());

        // filter frame for folder composite filters
        mFilterFrame.reset(new core::FilterFrame());
        mFilterFrame->resize(mProject.attribute().imageSize());

        // create texturizer for destination colors of the framebuffer
        mDestinationTexturizer.reset(new core::DestinationTexturizer());
        mDestinationTexturizer->resize(mProject.attribute().imageSize());
    }

    mExporting = true;
    return Result(ResultCode_Success, "Success.");
}

bool Exporter::updateTime(core::TimeInfo& aDst) {
    aDst = mOriginTimeInfo;

    const int range = mCommonParam.frame.diff() + 1;
    const double current = (mIndex * mOriginTimeInfo.fps) / (double)mCommonParam.fps;
    const double frame = mCommonParam.frame.min() + current;

    // end of export
    if (0 < mIndex && mCommonParam.frame.max() < frame) {
        return false;
    }
    if (mOriginTimeInfo.frameRange.max() < (int)frame) {
        return false;
    }

    aDst.frame = core::Frame::fromDecimal(frame);
    mProgress = (float)current / range;

    // to next index
    ++mIndex;

    return true;
}

bool Exporter::update() {
    if (!mExporting) {
        return false;
    }

    const int currentIndex = mIndex;

    // setup parameter
    core::TimeInfo timeInfo;
    if (!updateTime(timeInfo)) {
        return false;
    }

    // begin rendering
    gl::Global::makeCurrent();
    gl::Global::Functions& ggl = gl::Global::functions();
    const QSize originSize = mProject.attribute().imageSize();

    // clear clipping
    mClippingFrame->clearTexture();
    mClippingFrame->resetClippingId();
    mFilterFrame->clearAll();

    // clear destination texture
    mDestinationTexturizer->clearTexture();

    // bind framebuffer
    if (!mFramebuffers.front()->bind()) {
        XC_FATAL_ERROR("OpenGL Error", "Failed to bind framebuffer.", "");
    }

    // setup
    gl::Util::setViewportAsActualPixels(originSize);
    gl::Util::clearColorBuffer(0.0, 0.0, 0.0, 0.0);
    gl::Util::resetRenderState();

    // render
    core::RenderInfo renderInfo;
    renderInfo.camera.reset(originSize, 1.0, originSize, QPoint());
    renderInfo.time = timeInfo;
    renderInfo.framebuffer = mFramebuffers.front()->handle();
    renderInfo.dest = mFramebuffers.front()->texture();
    renderInfo.isGrid = false;
    renderInfo.clippingId = 0;
    renderInfo.clippingFrame = mClippingFrame.data();
    renderInfo.destTexturizer = mDestinationTexturizer.data();
    renderInfo.filterFrame = mFilterFrame.data();
    renderInfo.opacityScale = 1.0f;

    XC_ASSERT(renderInfo.framebuffer != 0);
    XC_ASSERT(renderInfo.dest != 0);
    mProject.objectTree().render(renderInfo, true);

    // unbind framebuffer
    if (!mFramebuffers.front()->release()) {
        XC_FATAL_ERROR("OpenGL Error", "Failed to bind framebuffer.", "");
    }

    // scaling
    {
        QOpenGLFramebufferObject* prev = nullptr;
        for (auto& fbo : mFramebuffers) {
            if (prev) {
                fbo->bind();

                const QSize size = fbo->size();
                gl::Util::setViewportAsActualPixels(size);
                gl::Util::clearColorBuffer(0.0, 0.0, 0.0, 0.0);

                mTextureDrawer.draw(prev->texture());

                fbo->release();
            }
            prev = fbo.get();
        }
    }

    {
        // create image
        auto outImage = mFramebuffers.back()->toImage();

        // flush
        ggl.glFlush();

        // update log if necessary
        updateLog();

        // export
        if (!exportImage(outImage, currentIndex)) {
            return false;
        }
    }

    return true;
}

bool Exporter::exportImage(const QImage& aFboImage, int aIndex) {
    // GIF mode: accumulate the rendered frames (encoded in finish()).
    if (mCollectFrames) {
        mGifFrames.append(aFboImage);
        return true;
    }

    // decide file path
    QFileInfo filePath;
    if (!decideImagePath(aIndex, filePath)) {
        return false;
    }

    if (mVideoExporting) {
        // Feed the frame to the in-process encoder (no intermediate raster,
        // no CLI subprocess).
        if (!mVideoEncoder.write(aFboImage)) {
            mLog = "Video encoder error.\n" + mVideoEncoder.error();
            return false;
        }
    } else {
        // QImage image(aFboImage.constBits(), aFboImage.width(),
        //              aFboImage.height(), QImage::Format_ARGB32);
        // image.save(aFilePath);
        aFboImage.save(filePath.filePath(), Q_NULLPTR, mImageParam.quality);
    }

    return true;
}

Exporter::Result Exporter::finish() {
    Result result(ResultCode_Success, "Success.");

    if (mExporting) {
        if (mCollectFrames) {
            // Encode the collected frames with the in-process GifEncoder.
            media::GifEncoder gif;
            media::GifEncoder::Config gcfg;
            gcfg.outputPath = mCommonParam.path;
            gcfg.fps = mCommonParam.fps > 0 ? mCommonParam.fps : 15;
            gcfg.transparent = mGifTransparent; // honor alpha if requested
            gcfg.palettePath = mGifPalettePath; // empty => adaptive palette
            QString gerr;
            if (!gif.encode(mGifFrames, gcfg, &gerr)) {
                mLog = "GIF encoder error.\n" + gerr;
                result = Result(ResultCode_EncodeError, mLog);
            }
            mCollectFrames = false;
            mGifFrames.clear();
        } else if (mVideoExporting) {
            // Mix the supplied multi-track audio (in project frames) into the
            // export window and mux it with the video in the same pass.
            if (mHasAudio) {
                media::AudioMixer mixer;
                media::AudioMixer::Config acfg;
                acfg.fps = mCommonParam.fps;
                acfg.totalFrames = mCommonParam.frame.diff() + 1; // export length
                acfg.sampleRate = mAudioSampleRate;
                acfg.channels = mAudioChannels;
                QVector<media::AudioMixer::Track> tracks = mAudioTracks;
                const int minFrame = mCommonParam.frame.min();
                for (auto& t : tracks) {
                    t.startFrame -= minFrame;
                    if (t.endFrame > 0)
                        t.endFrame -= minFrame;
                }
                if (mixer.mix(tracks, acfg)) {
                    mVideoEncoder.setAudio(mixer.samples(), mixer.channels(),
                                           mixer.sampleRate(), mixer.sampleCount());
                } else {
                    mLog = "Audio mixing error.\n" + mixer.error();
                    result = Result(ResultCode_EncodeError, mLog);
                }
            }

            if (!mVideoEncoder.close()) {
                mLog = "Video encoder error.\n" + mVideoEncoder.error();
                result = Result(ResultCode_EncodeError, mLog);
            }
        }

        mExporting = false;
    }
    return result;
}

void Exporter::destroyFramebuffers() {
    for (auto& fbo : mFramebuffers) {
        fbo.reset();
    }
}

void Exporter::createFramebuffers(const QSize& aOriginSize, const QSize& aExportSize) {
    destroyFramebuffers();

    mFramebuffers.emplace_back(FramebufferPtr(new QOpenGLFramebufferObject(aOriginSize)));

    // setup buffers for scaling
    if (aOriginSize != aExportSize) {
        static const int kMaxCount = 3;
        QSize size = aOriginSize;

        for (int i = 0; i < kMaxCount; ++i) {
            const double scaleX = aExportSize.width() / (double)size.width();
            const double scaleY = aExportSize.height() / (double)size.height();
            const double scaleMax = std::max(scaleX, scaleY);

            if (scaleMax >= 0.5 || i == kMaxCount - 1) {
                mFramebuffers.emplace_back(FramebufferPtr(new QOpenGLFramebufferObject(aExportSize)));
            }
            else {
                size.setWidth((int)(size.width() * 0.5));
                size.setHeight((int)(size.height() * 0.5));
                mFramebuffers.emplace_back(FramebufferPtr(new QOpenGLFramebufferObject(size)));
            }
        }
    }

    // setup texture
    for (auto& fbo : mFramebuffers) {
        setTextureParam(*fbo);
    }
}

void Exporter::setTextureParam(QOpenGLFramebufferObject& aFbo) {
    auto id = aFbo.texture();
    gl::Global::Functions& ggl = gl::Global::functions();
    ggl.glBindTexture(GL_TEXTURE_2D, id);
    ggl.glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ggl.glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ggl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    ggl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    ggl.glBindTexture(GL_TEXTURE_2D, 0);
}

bool Exporter::decideImagePath(int aIndex, QFileInfo& aPath) {
    const QString number = QString("%1").arg(aIndex, mDigitCount, 10, QChar('0'));
    QFileInfo filePath(mCommonParam.path + "/" + mImageParam.name + number + "." + mImageParam.suffix);

    // check overwrite
    if (!checkOverwriting(filePath)) {
        return false;
    }

    aPath = filePath;
    return true;
}

bool Exporter::checkOverwriting(const QFileInfo& aPath) {
    // check overwrite
    if (!mOverwriteConfirmation && aPath.exists()) {
        if (aPath.isDir() || !mOverwriteConfirmer || !mOverwriteConfirmer(aPath.filePath())) {
            return false;
        }
        mOverwriteConfirmation = true;
    }
    return true;
}

int Exporter::getDigitCount(const util::Range& aRange, int aFps, int aFpsOrigin) {
    int digitCount = 1;
    double count = (aFps * (aRange.diff() + 1)) / (double)aFpsOrigin;
    for (; count >= 10.0; count *= 0.1)
        ++digitCount;
    return std::max(digitCount, 4);
}

void Exporter::updateLog() {
    // In-process media encoding has no subprocess log to surface.
    Q_UNUSED(mUILogger);
}

} // namespace ctrl
