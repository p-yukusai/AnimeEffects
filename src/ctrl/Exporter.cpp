#include <QFileInfo>
#include <QBuffer>
#include <QApplication>
#include "util/SelectArgs.h"
#include "gl/Global.h"
#include "gl/Util.h"
#include "ctrl/Exporter.h"

#include <QCoreApplication>
#include "media/FFmpegHelpers.h"
#include "media/GifEncoder.h"

namespace ctrl {
//-------------------------------------------------------------------------------------------------
Exporter::Result::Result(): code(ResultCode_TERM) {}

Exporter::Result::Result(ResultCode aCode, const QString& aMessage): code(aCode), message(aMessage) {}

//-------------------------------------------------------------------------------------------------
Exporter::CommonParam::CommonParam(): path(), size(), selection(), fps() {}

bool Exporter::CommonParam::isValid() const {
    if (selection.isEmpty()) {
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
Exporter::GifParam::GifParam() {
    // palettePath defaults to empty (adaptive palette).
}

//-------------------------------------------------------------------------------------------------
Exporter::VideoParam::VideoParam(): container(), colorIndex(), bps(), pixfmt(), codec(), transparent(false) {}

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
    releaseExportVAO();
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
        mLog = QCoreApplication::translate("Exporter", "Invalid common parameters.");
        return Result(ResultCode_InvalidOperation, mLog);
    }

    // check directory
    QFileInfo path(aCommon.path);
    if (!path.exists() || !path.isDir()) {
        mLog = QCoreApplication::translate("Exporter", "Invalid directory path.");
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
        mLog = QCoreApplication::translate("Exporter", "Invalid common parameters.");
        return Result(ResultCode_InvalidOperation, mLog);
    }

    // check file overwriting
    mOverwriteConfirmation = false;
    QFileInfo filePath(aCommon.path);
    if (!checkOverwriting(filePath)) {
        mLog = QCoreApplication::translate("Exporter", "Exporting was canceled.");
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
        mLog = QCoreApplication::translate("Exporter", "Invalid common parameters.");
        return Result(ResultCode_InvalidOperation, mLog);
    }

    // check file overwriting
    mOverwriteConfirmation = false;
    QFileInfo filePath(aCommon.path);
    if (!checkOverwriting(filePath)) {
        mLog = QCoreApplication::translate("Exporter", "Exporting was canceled.");
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

        // The container decides the muxer, so it is the thing every other
        // question is asked against. Empty means the historical default, mp4.
        const QString container = aVideo.container.isEmpty() ? QStringLiteral("mp4")
                                                             : aVideo.container;

        // Codec: explicit > the container's probed default. A hand-written
        // preset used to sit between the two (a table of friendly codec names per
        // container); it could not be trusted, because it listed encoders a build
        // may not have and encoders a container cannot carry, and the exporter had
        // to re-validate every one of them anyway.
        QString codecName = aVideo.codec;
        if (codecName.isEmpty()) {
            codecName = media::ffmpeg::defaultCodecForContainer(container);
            if (codecName.isEmpty()) {
                mLog = QCoreApplication::translate(
                           "Exporter", "No available video encoder can carry the '%1' container.")
                           .arg(container);
                return Result(ResultCode_EncoderOpenFailed, mLog);
            }
        }

        // Resolve friendly names (e.g. "h264" -> "libx264") and verify the muxer
        // can actually hold this codec before we open the encoder. This turns a
        // container/codec mismatch into a clean rejection instead of a confusing
        // muxer failure at write time.
        codecName = media::ffmpeg::resolveEncoderName(codecName);
        if (codecName.isEmpty()) {
            mLog = QCoreApplication::translate("Exporter",
                                               "Encoder not available in this build: %1")
                       .arg(aVideo.codec);
            return Result(ResultCode_EncoderOpenFailed, mLog);
        }
        if (!media::ffmpeg::muxerSupportsCodec(container, codecName)) {
            mLog = QCoreApplication::translate(
                       "Exporter", "The '%1' container cannot carry the %2 video encoder.")
                       .arg(container, codecName);
            return Result(ResultCode_EncoderOpenFailed, mLog);
        }

        QString encoderError;
        media::VideoEncoder::Config cfg = media::VideoEncoder::makeConfig(
            container, codecName, aVideo.pixfmt, aVideo.transparent,
            mCommonParam.size.width(), mCommonParam.size.height(), mCommonParam.fps,
            aVideo.bps, &encoderError);

        if (cfg.codec.isEmpty()) {
            mLog = QCoreApplication::translate("Exporter", "Video encoder error.\n%1")
                       .arg(encoderError);
            return Result(ResultCode_EncoderOpenFailed, mLog);
        }
        cfg.outputPath = outPath;
        // The destination's encoder-level requirements: its private options and
        // its stream codec tag (hvc1 for Apple HEVC), plus how to reconcile the
        // rendered frame with an exact output size.
        cfg.encOptions = aVideo.encOptions;
        cfg.codecTag = aVideo.codecTag;
        cfg.fit = aCommon.fit;

        // Configure the audio stream (muxed in the same pass) when tracks were
        // supplied AND the container supports a standard audio codec we have.
        // This keeps AVI/Ogg exports video-only rather than failing on an
        // audio codec the container can't carry.
        if (mHasAudio) {
            if (media::ffmpeg::containerCarriesAudio(container)) {
                // The destination's audio spec wins; the container-derived
                // default is the fallback (WebM cannot carry AAC, so the codec
                // is never simply taken from the table without this check).
                QString audioCodec = aVideo.audioCodec;
                if (audioCodec.isEmpty()) {
                    audioCodec = container == QLatin1String("webm")
                                     ? QStringLiteral("libopus")
                                     : QStringLiteral("aac");
                }
                if (media::ffmpeg::encoderAvailable(audioCodec.toUtf8().constData())) {
                    cfg.audioCodec = audioCodec;
                    cfg.audioChannels = aVideo.audioChannels > 0 ? aVideo.audioChannels : mAudioChannels;
                    cfg.audioSampleRate = aVideo.audioSampleRate > 0 ? aVideo.audioSampleRate : mAudioSampleRate;
                    cfg.audioBitrate = aVideo.audioBitrate > 0 ? aVideo.audioBitrate : 128000;
                }
            }
        }

        // Frame-sink mode writes no file: the caller fits and encodes the frames
        // itself. Opening the encoder here would create an empty file that the
        // caller's own encode then has to overwrite.
        if (!mFrameSink && !mVideoEncoder.open(cfg)) {
            mLog = QCoreApplication::translate("Exporter", "Video encoder error.\n%1")
                       .arg(mVideoEncoder.error());
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
                mLog = QCoreApplication::translate("Exporter", "Export was canceled.");
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
    // Flatten the selection once: every frame that will be rendered, in output
    // order. updateTime() indexes this instead of assuming min() + n, which is
    // what made several ranges export as one span.
    mSelectedFrames = mCommonParam.selection.indices();
    mDigitCount = getDigitCount((int)mSelectedFrames.size(), mCommonParam.fps, mOriginTimeInfo.fps);

    // initialize graphics
    {
        gl::Global::makeCurrent();

        // Bind a vertex array object for the whole export (see mExportVAO).
        // Nothing else in the draw path can substitute for it, and without it the
        // failure is invisible: draws are dropped under a Core profile, the
        // transform-feedback pass that computes mesh positions produces zeros,
        // and the exporter writes a complete, well-formed file of empty frames.
        mExportVAO.reset(new gl::VertexArrayObject());
        gl::Global::functions().glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &mPreviousVAO);
        mExportVAO->bind();

        // texture drawer
        if (!mTextureDrawer.init()) {
            mLog = QCoreApplication::translate("Exporter", "Failed to initialize TextureDrawer");
            // This Result used to be built and discarded, so the export carried on with
            // an uninitialised drawer and the failure was never reported to anyone.
            return Result(ResultCode_UnclassfiedError, mLog);
        }

        // framebuffers scale from the native size to what we RENDER at, which is
        // the output size unless the destination asked for an exact size that the
        // project's aspect does not match (then renderSize, and the encoder pads).
        const QSize renderTarget = (mCommonParam.renderSize.isValid() && !mCommonParam.renderSize.isEmpty())
                                       ? mCommonParam.renderSize
                                       : mCommonParam.size;
        createFramebuffers(mProject.attribute().imageSize(), renderTarget);

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

    // Output frame mIndex maps onto a position in the flattened selection,
    // scaled by the fps ratio (the export fps need not match the project's).
    const double current = (mIndex * mOriginTimeInfo.fps) / (double)mCommonParam.fps;
    const int base = (int)current;
    const int count = (int)mSelectedFrames.size();

    // end of export (same boundary as before: a fractional overshoot past the
    // last selected frame ends the run rather than emitting a tail frame)
    if (base >= count || (0 < mIndex && current > (double)(count - 1))) {
        return false;
    }
    if (mOriginTimeInfo.frameRange.max() < mSelectedFrames[base]) {
        return false;
    }

    // Interpolate toward the NEXT SELECTED frame, not the next project frame:
    // with several ranges the following output frame is not frame + 1.
    const double frac = current - base;
    const int next = std::min(base + 1, count - 1);
    const double frame = mSelectedFrames[base]
                         + frac * (double)(mSelectedFrames[next] - mSelectedFrames[base]);

    aDst.frame = core::Frame::fromDecimal(frame);
    mProgress = (float)current / (double)count;

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
    // Frame-sink mode: nothing is written here. The frames are handed over in
    // finish(), at the render size, for the caller to fit and encode.
    if (mFrameSink) {
        mSinkFrames.append(aFboImage);
        return true;
    }

    // Frames leave here at the RENDER size; an exact-size destination needs the
    // output size, so reconcile once here for the paths that do not go through
    // the encoder (which does its own pad/crop from the same helper).
    const QImage outImage = media::fitImage(aFboImage, mCommonParam.size, mCommonParam.fit);

    // GIF mode: accumulate the rendered frames (encoded in finish()).
    if (mCollectFrames) {
        mGifFrames.append(outImage);
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
            mLog = QCoreApplication::translate("Exporter", "Video encoder error.\n%1")
                       .arg(mVideoEncoder.error());
            return false;
        }
    } else {
        // QImage image(aFboImage.constBits(), aFboImage.width(),
        //              aFboImage.height(), QImage::Format_ARGB32);
        // image.save(aFilePath);
        outImage.save(filePath.filePath(), Q_NULLPTR, mImageParam.quality);
    }

    return true;
}

Exporter::Result Exporter::finish() {
    Result result(ResultCode_Success, "Success.");

    if (mFrameSink && !mSinkFrames.isEmpty()) {
        FrameSink sink = mFrameSink;
        QList<QImage> frames = mSinkFrames;
        // Cleared BEFORE the call: the sink typically re-encodes, and a sink that
        // reported failure must not leave the exporter able to hand the same
        // frames over twice.
        mSinkFrames.clear();
        mFrameSink = nullptr;
        sink(frames);
        mExporting = false;
        releaseExportVAO();
        return result;
    }

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
                mLog = QCoreApplication::translate("Exporter", "GIF encoder error.\n%1").arg(gerr);
                result = Result(ResultCode_EncodeError, mLog);
            }
            mCollectFrames = false;
            mGifFrames.clear();
        } else if (mVideoExporting) {
            // Mix the supplied multi-track audio and mux it with the video in the
            // same pass.
            //
            // One mix per SELECTED RANGE, concatenated in order, because the
            // exporter writes the selected ranges back to back (see FrameSelection):
            // a gapped selection plays 0-1 then 10-11 as four consecutive frames, so
            // its audio is those same four frames' sound, in that order. The mixer
            // takes one window at a time; mixing the whole span instead would lay
            // down eight frames of sound under four frames of picture.
            if (mHasAudio) {
                media::AudioMixer::Config base;
                base.fps = mCommonParam.fps;
                base.sampleRate = mAudioSampleRate;
                base.channels = mAudioChannels;

                QVector<float> all;
                int gotChannels = 0;
                int gotRate = 0;
                bool mixFailed = false;
                for (const FrameRange& range : mCommonParam.selection.ranges) {
                    if (!range.valid() || range.count() <= 0)
                        continue;
                    media::AudioMixer::Config cfg = base;
                    cfg.totalFrames = range.count();
                    // Tracks are in project frames; this window starts at the
                    // range's first frame, exactly as the single-window case did.
                    QVector<media::AudioMixer::Track> tracks = mAudioTracks;
                    for (auto& t : tracks) {
                        t.startFrame -= range.first;
                        if (t.endFrame > 0)
                            t.endFrame -= range.first;
                    }
                    media::AudioMixer chunk;
                    if (!chunk.mix(tracks, cfg)) {
                        mLog = QCoreApplication::translate("Exporter", "Audio mixing error.\n%1")
                                   .arg(chunk.error());
                        result = Result(ResultCode_EncodeError, mLog);
                        mixFailed = true;
                        break;
                    }
                    if (all.isEmpty()) {
                        gotChannels = chunk.channels();
                        gotRate = chunk.sampleRate();
                    }
                    const int count = chunk.sampleCount() * chunk.channels();
                    const float* src = chunk.samples();
                    for (int i = 0; i < count; ++i)
                        all.append(src[i]);
                }
                if (!mixFailed && !all.isEmpty() && gotChannels > 0) {
                    mVideoEncoder.setAudio(all.constData(), gotChannels, gotRate,
                                           all.size() / gotChannels);
                }
            }

            if (!mVideoEncoder.close()) {
                mLog = QCoreApplication::translate("Exporter", "Video encoder error.\n%1")
                       .arg(mVideoEncoder.error());
                result = Result(ResultCode_EncodeError, mLog);
            }
        }

        mExporting = false;
    }
    releaseExportVAO();
    return result;
}

void Exporter::releaseExportVAO() {
    if (mExportVAO) {
        // Give the previous binding back BEFORE deleting our own: deleting a
        // bound VAO leaves the binding at 0, and a canvas that bound its VAO once
        // at initializeGL would then be drawing with none — turning a completed
        // export into a broken interactive view.
        gl::Global::functions().glBindVertexArray((GLuint)mPreviousVAO);
        mExportVAO.reset();
    }
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
    // A still destination writes the one file the caller named; everything else is
    // a numbered sequence. The index is ignored in the single-file case, so a
    // selection that somehow held two frames would still produce one image rather
    // than two files with the same name.
    const QString stem = mImageParam.singleFile
                             ? mImageParam.name
                             : mImageParam.name
                                   + QString("%1").arg(aIndex, mDigitCount, 10, QChar('0'));
    QFileInfo filePath(mCommonParam.path + "/" + stem + "." + mImageParam.suffix);

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

int Exporter::getDigitCount(int aFrameCount, int aFps, int aFpsOrigin) {
    int digitCount = 1;
    double count = (aFps * aFrameCount) / (double)aFpsOrigin;
    for (; count >= 10.0; count *= 0.1)
        ++digitCount;
    return std::max(digitCount, 4);
}

void Exporter::updateLog() {
    // In-process media encoding has no subprocess log to surface.
    Q_UNUSED(mUILogger);
}

} // namespace ctrl
