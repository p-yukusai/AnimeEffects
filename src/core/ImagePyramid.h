#ifndef CORE_IMAGEPYRAMID_H
#define CORE_IMAGEPYRAMID_H

#include <QSize>
#include <QtGlobal>
#include <vector>
#include "XC.h"
#include "util/NonCopyable.h"

namespace gl {
class Texture;
}

namespace core {

/// THE RAM SIDE OF A PAGED IMAGE: every level of the chain the GPU built, kept where a page
/// upload can read it.
///
/// It stores levels 1..N and NOT level 0, which is the larger half of the chain: a level-0 page
/// is a straight copy of the resource's own pixels with the premultiply applied, and the resource
/// already holds those pixels - keeping another 4 bytes per texel of the base would double the
/// cost of every paged image for a memcpy. Levels 1 and up are the areas a page cannot recompute
/// cheaply, so they are captured once from the chain the renderer already builds.
///
/// WHY CAPTURE FROM THE GPU CHAIN, rather than build the levels on the CPU: the levels a page
/// serves have to be the ones the full-resolution control path serves, or the gate that compares
/// the two paths is measuring the wrong thing. capture() reads the same GL texture the control
/// samples, level for level, so the paged path and the control differ only in how the texels are
/// addressed. The rule that built them (gl::Texture::kMipChainSpan and kMipStepShrink, with
/// glGenerateMipmap's exact-halving levels) lives in one place and does not fork.
///
/// The capture is a synchronous readback, once per resource, at the moment its ImageKey is
/// created: the design measured a whole chain build at under 1 % of project load, and this adds
/// one copy of the chain's 33 % to the load a resource already pays for its image. Nothing reads
/// back during a frame.
class ImagePyramid: private util::NonCopyable {
public:
    ImagePyramid();
    ~ImagePyramid();

    /// Copies levels 1..aLastLevel of an existing chain texture into RAM. The texture must have
    /// at least aLastLevel+1 levels. Returns false (and clears) when no level could be read.
    bool capture(gl::Texture& aChain, int aLastLevel);

    void clear();

    /// Drops every captured level deeper than aMaxLevel (used after the coarse chain has been
    /// copied out: those levels are always resident as a texture and never uploaded as pages).
    void truncate(int aMaxLevel);

    bool isValid() const { return mSize.isValid(); }
    const QSize& size() const { return mSize; }
    /// The deepest level this pyramid stores (1 when only level 1 was captured).
    int maxLevel() const { return mMaxLevel; }
    int chainLevels() const { return mChainLevels; }
    qint64 bytes() const;

    /// Writes one pool page: kPitch x kPitch premultiplied RGBA8 texels, the 256x256 content
    /// plus the gutter, for page (aPageX, aPageY) at `aLevel`. Level 0 is built from aBase
    /// (STRAIGHT RGBA8, the resource's own pixels, aBaseSize texels); higher levels are copied
    /// from the captured data. Texels outside the level are transparent, which is what the
    /// sampler's clamp-to-border did for the full texture.
    ///
    /// The caller owns `aDst` and must have it at least kPitch*kPitch*4 bytes.
    void fillPage(int aLevel, int aPageX, int aPageY, const uint8* aBase, const QSize& aBaseSize, uint8* aDst) const;

    /// The level's own texel size, `max(1, size >> level)` - exactly the size of that level in
    /// the full chain (see gl::Texture).
    QSize levelSize(int aLevel) const;

    /// Copies one whole level into a caller's tightly packed RGBA8 buffer of aSize texels.
    /// Used to upload the always-resident coarse chain from the same bytes the page path reads.
    void copyLevel(int aLevel, uint8* aDst, const QSize& aSize) const;

    /// FNV-1a over one captured level (diagnostic: is the captured level the chain's level).
    quint64 hashLevel(int aLevel) const;
    /// FNV-1a over a level's rectangle (diagnostic: is the page the level's own texels).
    quint64 hashRegion(int aLevel, int aX, int aY, int aWidth, int aHeight) const;

private:
    QSize mSize;
    int mChainLevels = 0;
    int mMaxLevel = 0;
    // index by level; mLevels[0] is empty by design (see the class comment)
    std::vector<std::vector<uint8>> mLevels;
};

} // namespace core

#endif // CORE_IMAGEPYRAMID_H
