#ifndef GUI_THUMBNAILCACHE_H
#define GUI_THUMBNAILCACHE_H

#include <QHash>
#include <QPixmap>
#include <QSize>
#include "img/Buffer.h"

namespace gui {

// LAYER THUMBNAILS for the object tree: a layer's own source pixels at row size,
// cached, so the tree's repaint never scales a full image.
//
// The semantic is RECOGNITION, not "the current result": a layer's source image is what
// identifies it ("which layer is this?"), it never changes behind the user's back, and
// it needs no GL. What the matte hides is the matte chip's job, not the thumbnail's.
//
// Folders deliberately have no thumbnail. A folder has no source pixels of its own - its
// content IS a composite - so a folder thumbnail could only be a render through the real
// renderer or a second, CPU compositor that would drift from it (blend modes, filters,
// mattes, clipping). Leaves have nothing to composite, which is exactly why this is
// cheap for them and not worth doing for folders.
//
// COST, measured (2026-10-26, this machine):
//   * 606x530 source -> 18px, direct smooth:            0.408 ms
//   * 16.9 Mpx source -> 18px, direct smooth:           75.7 ms   <- a hitch
//   * 16.9 Mpx source -> 1/8 fast -> 18px smooth:        2.33 ms  <- what this does
// so a source above kSubsamplePixels is reduced by a power of two FIRST, which costs
// quality nothing at 18px and bounds the worst case at a couple of milliseconds.
class ThumbnailCache {
public:
    // The pixmap for this buffer, fitted inside an aSize LOGICAL square and rendered at
    // aDpr device pixels per logical pixel. The returned pixmap carries its own
    // devicePixelRatio, so Qt paints it 1:1 - resampling it here and letting the view
    // resample again is what made the first version soft.
    //
    // The returned pixmap is the cell's box PLUS a 1px frame ring, so the frame is drawn in
    // the space the row's icon slot leaves free rather than over the art's own edge - the
    // first version painted it on the art's outermost pixel, where it disappeared on any
    // bright layer (reported as "the border is barely visible").
    //
    // Its SIZE is the artwork's ASPECT (a 600x160 layer gives 22x6, not 22x22 with bands):
    // the checker is a backdrop for ALPHA, not for letterboxing, so there is nothing to
    // letterbox - the row centres the strip and the checker only shows through pixels the
    // layer actually leaves transparent.
    QPixmap layerThumbnail(const img::Buffer& aImage, int aSize, qreal aDpr = 1.0);

    // The key a caller can store beside its row to know when the image behind a
    // thumbnail has changed (a reimport replaces the buffer) or when the size it is
    // drawn at has.
    static quint64 key(const img::Buffer& aImage, int aSize, qreal aDpr = 1.0);

    void clear() { mCache.clear(); }

private:
    static QImage toImage(const img::Buffer& aImage);

    QHash<quint64, QPixmap> mCache;

    // Above this many source pixels the image is halved (repeatedly) before the smooth
    // scale: measured 32x cheaper on a 16.9 Mpx layer and indistinguishable at 22px.
    static constexpr int kSubsamplePixels = 1024 * 1024;

    // The alpha checker's square, in LOGICAL pixels: 5 divides the 20px cell exactly (four
    // tiles, no sliver), and the size is what lets the tiles stay dim - at 4px the pair had
    // to be 26 levels apart to read, at 5px 18 is enough, so the cell no longer lifts off
    // the row (measured: 69/43 was reported as too bright; 46/28 sits at the row's value).
    static constexpr int kCheckerSquare = 5;

    // The cell's corner rounding, in LOGICAL pixels (the review asked for a few pixels of
    // it). The frame follows the same path, so the two cannot disagree at the corner.
    static constexpr int kCornerRadius = 2;
};

} // namespace gui

#endif // GUI_THUMBNAILCACHE_H
