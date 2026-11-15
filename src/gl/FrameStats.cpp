#include "gl/FrameStats.h"

#include <QFile>
#include <QtGlobal>

#include <cstdio>

namespace gl {

FrameStats& FrameStats::instance() {
    static FrameStats stats;
    return stats;
}

FrameStats::FrameStats() {
    const QByteArray path = qgetenv("AE_FRAME_STATS");
    mPath = QString::fromUtf8(path);
    mEnabled = !mPath.isEmpty();
}

void FrameStats::beginFrame() {
    if (!mEnabled)
        return;
    ++mFrameIndex;
    mLayersConsidered = 0;
    mLayersDrawn = 0;
    mLayersCulled = 0;
    mMeshPasses = 0;
    mFillPx = 0.0;
    // a frame that measures nothing must read as zeros, never as the previous frame's
    // distribution (the readback only replaces these when it has a fresh result)
    for (int i = 0; i < kDensityBins; ++i)
        mDensityBins[i] = 0.0;
    mDensityMax = 0.0;
    for (int i = 0; i < kSmaxBins; ++i)
        mSmaxBins[i] = 0.0;
    mSmaxMax = 0.0;
    for (int i = 0; i < kAnisoBins; ++i)
        mAnisoBins[i] = 0.0;
    mAnisoMax = 0.0;
    mFrameTimer.start();
}

void FrameStats::layerDrawn(double aScreenAreaPx) {
    if (!mEnabled)
        return;
    ++mLayersDrawn;
    mFillPx += aScreenAreaPx;
}

void FrameStats::textureCreated(qint64 aBytes) {
    if (!mEnabled)
        return;
    mLiveTextureBytes += aBytes;
    mPeakTextureBytes = qMax(mPeakTextureBytes, mLiveTextureBytes);
}

void FrameStats::textureDestroyed(qint64 aBytes) {
    if (!mEnabled)
        return;
    mLiveTextureBytes -= aBytes;
    if (mLiveTextureBytes < 0)
        mLiveTextureBytes = 0;
}

void FrameStats::densityHistogram(const float* aBins, float aMaxDensity) {
    if (!mEnabled || !aBins)
        return;
    for (int i = 0; i < kDensityBins; ++i) {
        const double area = aBins[i];
        // the GPU can only produce finite sums of finite areas, but a line with a NaN in
        // it is a line no probe can parse, so the boundary is checked here
        mDensityBins[i] = (area > 0.0 && area < 1.0e30) ? area : 0.0;
    }
    mDensityMax = (aMaxDensity > 0.0f && aMaxDensity < 1.0e30f) ? aMaxDensity : 0.0;
    if (mDensityMax > mSessionDensityMax)
        mSessionDensityMax = mDensityMax;
}

void FrameStats::anisotropyHistogram(
    const float* aSmaxBins, float aMaxSmax, const float* aAnisoBins, float aMaxAniso
) {
    if (!mEnabled)
        return;
    if (aSmaxBins) {
        for (int i = 0; i < kSmaxBins; ++i) {
            const double area = aSmaxBins[i];
            mSmaxBins[i] = (area > 0.0 && area < 1.0e30) ? area : 0.0;
        }
    }
    if (aAnisoBins) {
        for (int i = 0; i < kAnisoBins; ++i) {
            const double area = aAnisoBins[i];
            mAnisoBins[i] = (area > 0.0 && area < 1.0e30) ? area : 0.0;
        }
    }
    // a NaN or an infinity here would make the line unparseable, exactly as for dens_max; a
    // singular Jacobian is clamped in the shader, so the boundary is checked again on arrival
    mSmaxMax = (aMaxSmax > 0.0f && aMaxSmax < 1.0e30f) ? aMaxSmax : 0.0;
    mAnisoMax = (aMaxAniso >= 0.0f && aMaxAniso < 1.0e30f) ? aMaxAniso : 0.0;
}

void FrameStats::endFrame(int aViewWidth, int aViewHeight) {
    if (!mEnabled)
        return;
    ++mFrame;
    QFile file(mPath);
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;

    // the bins travel as one bracketed list so the line stays a flat key=value set:
    // splitting on whitespace gives `dens=[..]` and `dens_max=..` like every other field
    QString density;
    for (int i = 0; i < kDensityBins; ++i) {
        if (i > 0)
            density += QLatin1Char(',');
        density += QString::number(mDensityBins[i], 'f', 2);
    }
    QString smax;
    for (int i = 0; i < kSmaxBins; ++i) {
        if (i > 0)
            smax += QLatin1Char(',');
        smax += QString::number(mSmaxBins[i], 'f', 2);
    }
    QString aniso;
    for (int i = 0; i < kAnisoBins; ++i) {
        if (i > 0)
            aniso += QLatin1Char(',');
        aniso += QString::number(mAnisoBins[i], 'f', 2);
    }

    // one line per frame: a probe can diff two frames or average a run without a parser
    const QString line = QStringLiteral(
        "frame=%1 ms=%2 view=%3x%4 layers=%5/%6/%7 mesh=%8 fill_px=%9 tex_live=%10 tex_peak=%11 mask_bytes=%12 "
        "dens=[%13] dens_max=%14 dens_smax=[%15] smax_max=%16 aniso=[%17] aniso_max=%18 "
        "vt_pages=%19 vt_bytes=%20 vt_up=%21 vt_evict=%22 vt_frame_ms=%23\n"
    ).arg(mFrame)
         .arg(mFrameTimer.isValid() ? mFrameTimer.nsecsElapsed() / 1000000.0 : 0.0, 0, 'f', 2)
         .arg(aViewWidth)
         .arg(aViewHeight)
         .arg(mLayersConsidered)
         .arg(mLayersDrawn)
         .arg(mLayersCulled)
         .arg(mMeshPasses)
         .arg(qint64(mFillPx))
         .arg(mLiveTextureBytes)
         .arg(mPeakTextureBytes)
         .arg(mMaskBytes)
         .arg(density)
         .arg(mDensityMax, 0, 'g', 6)
         .arg(smax)
         .arg(mSmaxMax, 0, 'g', 6)
         .arg(aniso)
         .arg(mAnisoMax, 0, 'g', 6)
         .arg(mVtPages)
         .arg(mVtBytes)
         .arg(mVtUploadBytes)
         .arg(mVtEvictions)
         .arg(mVtFrameMs, 0, 'f', 2);
    file.write(line.toUtf8());
    ++mFramesWritten;
}

QString FrameStats::summary() const {
    return QStringLiteral(
        "frames=%1 tex_live=%2 tex_peak=%3 mask_bytes=%4 dens_max_session=%5"
    ).arg(mFramesWritten)
     .arg(mLiveTextureBytes)
     .arg(mPeakTextureBytes)
     .arg(mMaskBytes)
     .arg(mSessionDensityMax, 0, 'g', 6);
}

} // namespace gl
