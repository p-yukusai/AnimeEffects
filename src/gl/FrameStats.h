#ifndef GL_FRAMESTATS_H
#define GL_FRAMESTATS_H

#include <QElapsedTimer>
#include <QString>
#include <QtGlobal>

namespace gl {

// WHAT A FRAME COSTS, counted where the engine spends it.
//
// Every hypothesis about this renderer - "tiles would help", "culling the layer is the win",
// "the masks are what fills VRAM" - is an argument until these numbers exist. The counter is OFF
// unless `AE_FRAME_STATS=<path>` is set, and each call is one predictable branch, so a release
// build pays nothing for it.
//
// What is counted, and what each number means:
//   * layers - considered / drawn / culled. "Culled" counts the ones a bounds test rejects; with
//     no bounds test in the engine yet it stays 0, which is itself the finding.
//   * fill_px - the sum of the drawn layers' SCREEN-SPACE quad areas. That is the fill the frame
//     asks for (each screen pixel under a layer is a sample), not a GPU counter: the engine targets
//     GL 3.3 core, where per-draw sample queries are not available portably.
//   * tex_live_bytes / tex_peak_bytes - the bytes of every gl::Texture ever created minus the ones
//     destroyed: the texture residency the process is holding right now.
//   * mask_bytes - the RAM of the document's MATTES: every node's blended MaskKey field (one
//     byte per content texel) plus the CPU pyramid a paged matte keeps for its levels, and every
//     filter lane's painted field. RAM, not VRAM - a chain's GPU bytes belong to tex_live. Fed
//     once per frame from ObjectTree::maskRamBytes, and only while the counter is on (the tree
//     walk is the cost).
//   * dens / dens_max - the DISTRIBUTION of the texel density the deformation produced, not just
//     its worst case. A triangle's density is its deformed content-space area divided by its uv
//     area in content pixels: 1.0 for a layer that is merely scaled uniformly, above 1 where the
//     deformation stretches it, below 1 where it compresses it. The 16 bins are centred on
//     log2(density) = -4, -3.5 ... +3.5 (so a density of 1.0, an undeformed triangle, is in the
//     middle of bin 8; bin 15 is open-ended and takes everything above it) and each holds the
//     SUM OF UV AREAS that fell into it, so an area-weighted percentile is a prefix sum over
//     them - which is what a mip level policy needs, and why a maximum alone is not enough.
//     dens_max is the largest density any triangle of the frame reached. Triangles whose uv
//     area is under a quarter of a pixel are not measured at all (see kMinUvArea in
//     MeshTransformer.cpp). Measured every frame (playback changes the blend and the
//     deformation every frame) by the density pass in MeshTransformer::callGL; see
//     DensityMeter there.
//
//   * dens_smax / smax_max - the same distribution over log2(s_max), where s_max is the LARGER
//     singular value of the triangle's 2x2 Jacobian (uv edges -> deformed mesh edges). Density
//     is only the product s_max * s_min, so a 4:1 footprint and a 2:2 one read the same area
//     ratio while needing different mip levels: the 4:1 needs the level its major axis asks for.
//     s_max is that level's answer, and it uses the density bins' base and step unchanged (so
//     log2(s_max) = 0, an undeformed triangle, is bin 8, and a 2x stretch on one axis - density
//     2 - is bin 10). smax_max is the largest s_max of the frame.
//
//   * aniso / aniso_max - the ratio s_max / s_min of the same triangle, histogrammed over the
//     edges 1.5, 2, 3, 4, 6, 8, 16 (kAnisoBinEdges) into 8 uv-area-weighted bins: bin 0 is a
//     square footprint (ratio under 1.5), bin 2 a 2:1 one, bin 7 everything from 16:1 up. This
//     is the factor by which a trilinear level chosen from the major axis over-blurs the minor
//     one, i.e. exactly what anisotropic filtering recovers. aniso_max is the largest ratio.
//
//     All three histograms share the triangle set and the sliver rule: a triangle whose uv area
//     is under kMinUvArea contributes to none of them. A singular Jacobian (degenerate triangle)
//     is clamped before log2 and before the ratio, so the line never carries a NaN or an inf.
//
//   * vt_pages / vt_bytes - the PAGED TEXTURE POOL (gl::PagePool): sharp pages resident at the
//     end of the frame, and the bytes those pages hold. `vt_up` is the bytes uploaded into the
//     pool this frame and `vt_evict` the pages evicted to make room. Zero when virtual texturing
//     is off (AE_VIRTUAL_TEXTURE=0), which is the A/B control.
//   * vt_frame_ms - the time VirtualTexture::beginFrame spent this frame: the previous frame's
//     demand copy is parsed, every missing wanted page is filled from the pyramid and uploaded,
//     and the tables are published. This is the LANDING cost of the paged machinery, and `ms`
//     includes it; it is the number the landing-rate question has to divide (vt_up bytes per
//     frame against this) before any upload strategy can be judged.
//
// Output: one line per frame appended to the path in AE_FRAME_STATS, plus a final summary line.
class FrameStats {
public:
    // the density histogram's shape (see the header comment); the density pass and this class
    // must agree on it, so both read these. Bins are CENTRED on base + b * step, so the
    // densities this engine actually produces (1.0 for an undeformed triangle) land in the
    // middle of a bin instead of on the boundary between two.
    enum {
        kDensityBins = 16,
        // log2(s_max) uses the density bins' base and step, so its shape is the same
        kSmaxBins = 16,
        // s_max / s_min bins, separated by the kAnisoBins - 1 interior edges below
        kAnisoBins = 8,
    };
    static constexpr float kDensityBinBase = -4.0f; // log2(density) of bin 0's centre
    static constexpr float kDensityBinStep = 0.5f;  // one bin per half octave
    // Interior edges of the anisotropy histogram, ascending: bin b holds the ratios in
    // [kAnisoBinEdges[b - 1], kAnisoBinEdges[b]) with bin 0 below the first edge and the last
    // bin open-ended above the last one. The geometry shader hardcodes the same edges (GLSL
    // cannot read this array), so the two must be changed together.
    static constexpr float kAnisoBinEdges[kAnisoBins - 1] = {
        1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 16.0f,
    };

    static FrameStats& instance();

    bool enabled() const { return mEnabled; }

    // Advances in beginFrame. The density readback in MeshTransformer owns a GPU-side
    // accumulator that has to be copied out and cleared once per frame, and this is the only
    // frame boundary it can see without a second clock of its own.
    quint64 frameIndex() const { return mFrameIndex; }

    // the viewport's frame
    void beginFrame();
    void endFrame(int aViewWidth, int aViewHeight);

    void layerConsidered() { if (mEnabled) ++mLayersConsidered; }
    void layerCulled() { if (mEnabled) ++mLayersCulled; }
    void layerDrawn(double aScreenAreaPx);
    // one transform-feedback vertex pass: the skinning every layer re-runs per painted frame,
    // even when only the camera moved (see MeshTransformer)
    void meshPass() { if (mEnabled) ++mMeshPasses; }
    void textureCreated(qint64 aBytes);
    void textureDestroyed(qint64 aBytes);
    void maskFieldBytes(qint64 aBytes) { if (mEnabled) mMaskBytes = aBytes; }

    /// The paged texture pool's frame (see gl::PagePool): pages resident at the end of the
    /// frame, their bytes, the bytes uploaded this frame, and the pages evicted.
    void virtualTexture(int aPages, qint64 aBytes, qint64 aUploadBytes, int aEvictions) {
        if (!mEnabled)
            return;
        mVtPages = aPages;
        mVtBytes = aBytes;
        mVtUploadBytes = aUploadBytes;
        mVtEvictions = aEvictions;
    }

    /// The time the paged textures' frame boundary took this frame (see the header comment).
    void virtualTextureFrameTime(double aMs) {
        if (!mEnabled)
            return;
        mVtFrameMs = aMs;
    }

    // One frame's measured distribution, replacing whatever the previous readback left.
    // `aBins` holds kDensityBins uv-area sums in bin order (bin b covers
    // [kDensityBinBase + b * kDensityBinStep, kDensityBinBase + (b + 1) * kDensityBinStep)
    // in log2 density), `aMaxDensity` the largest density measured (0 when nothing was).
    void densityHistogram(const float* aBins, float aMaxDensity);

    // The same, for the deformation's singular values: `aSmaxBins` holds kSmaxBins uv-area
    // sums over log2(s_max) in the density bins' base and step, `aAnisoBins` kAnisoBins sums
    // over s_max / s_min along kAnisoBinEdges. Both maxima are 0 when nothing was measured.
    void anisotropyHistogram(
        const float* aSmaxBins, float aMaxSmax, const float* aAnisoBins, float aMaxAniso
    );

    // one line, for a probe or a log
    QString summary() const;

private:
    FrameStats();

    bool mEnabled;
    QString mPath;
    qint64 mFrame = 0;
    // one per beginFrame; see frameIndex()
    quint64 mFrameIndex = 0;
    int mLayersConsidered = 0;
    int mLayersDrawn = 0;
    int mLayersCulled = 0;
    int mMeshPasses = 0;
    double mFillPx = 0.0;
    QElapsedTimer mFrameTimer;
    qint64 mLiveTextureBytes = 0;
    qint64 mPeakTextureBytes = 0;
    qint64 mMaskBytes = 0;
    qint64 mFramesWritten = 0;

    // this frame's density distribution (see the header comment), reset in beginFrame
    double mDensityBins[kDensityBins] = {};
    double mDensityMax = 0.0;
    // the same frame's s_max distribution and anisotropy distribution, also reset there
    double mSmaxBins[kSmaxBins] = {};
    double mSmaxMax = 0.0;
    double mAnisoBins[kAnisoBins] = {};
    double mAnisoMax = 0.0;
    // the largest density any frame of this run produced
    double mSessionDensityMax = 0.0;
    // the paged texture pool at the end of the frame (see virtualTexture)
    int mVtPages = 0;
    qint64 mVtBytes = 0;
    qint64 mVtUploadBytes = 0;
    int mVtEvictions = 0;
    // the landing cost of this frame's VirtualTexture::beginFrame, in ms (see the header comment)
    double mVtFrameMs = 0.0;
};

} // namespace gl

#endif // GL_FRAMESTATS_H
