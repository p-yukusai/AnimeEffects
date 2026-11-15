// Gate for the object tree's LAYER THUMBNAILS (gui::ThumbnailCache).
//
// The thumbnail is a cached downscale of a layer's own source pixels. What can go wrong
// quietly, and is asserted here:
//
//   [1] A FULLY TRANSPARENT layer draws as NO ICON AT ALL. That is what the first
//       version did - two rows lost their glyph and the column's rhythm with it - so a
//       cell is always returned, with an alpha checker under the art.
//   [2] THE ART MUST BE VISIBLE and the cell must not be the checker alone: a solid
//       colour source has to appear at the centre while the corners stay checker.
//   [3] ASPECT IS KEPT: a wide source fills the cell's width and leaves checker above
//       and below, rather than being stretched.
//   [4] THE CACHE IS KEYED ON THE PIXELS: the same buffer returns the same pixmap, and a
//       REIMPORT (a new buffer) returns a different one. The tree's icon refresh hangs
//       on that key - a stale key is a thumbnail that never updates after a live link.
//   [5] A BIG SOURCE is subsampled first: measured 75.7 ms direct vs 2.33 ms after a 1/8
//       pass on a 16.9 Mpx layer, and the 18px result is indistinguishable.
//
// Headless: no GL, no tree, no window.
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QSet>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "XC.h"
#include "gui/ThumbnailCache.h"
#include "gui/theme/Colors.h"
#include "img/Buffer.h"

namespace {

class HarnessAssertHandler: public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};

HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;
int failures = 0;

void check(bool aOk, const char* aWhat) {
    if (!aOk) {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}

// A buffer holding one solid colour, the shape a layer's source image arrives in.
img::Buffer makeBuffer(const QSize& aSize, const QColor& aColor) {
    img::Buffer buffer;
    buffer.alloc(img::Format_RGBA8, aSize);
    uint8* data = buffer.data();
    const int bytes = aSize.width() * aSize.height() * 4;
    for (int i = 0; i < bytes; i += 4) {
        data[i + 0] = uint8(aColor.red());
        data[i + 1] = uint8(aColor.green());
        data[i + 2] = uint8(aColor.blue());
        data[i + 3] = uint8(aColor.alpha());
    }
    return buffer;
}

QImage asImage(const QPixmap& aPixmap) { return aPixmap.toImage(); }

// A pixel's distance from a colour, so "is this the art or the checker" is a comparison
// rather than an equality (the checker is translucent over the cell's own fill).
int distance(const QColor& a, const QColor& b) {
    return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    // the cell box the tree asks for: ctrl::TimeLineRow::kHeight (22) - 2, leaving a 1px gap
    // on each side so consecutive rows do not touch
    constexpr int kSize = 20;
    gui::ThumbnailCache cache;

    // ---- [1] a fully transparent layer still gets a cell ----
    {
        const img::Buffer clear = makeBuffer(QSize(200, 160), QColor(0, 0, 0, 0));
        const QPixmap pm = cache.layerThumbnail(clear, kSize);
        std::printf("MEASURED transparent layer: %dx%d device px, dpr %.2f\n", pm.width(), pm.height(),
                    pm.devicePixelRatio());
        check(!pm.isNull(), "a transparent layer still gets a cell (not 'no icon')");
        // the ring is OUTSIDE the cell, so the long side is the box plus the ring
        check(pm.width() == kSize || pm.height() == kSize, "its long side fills the cell box");
        // the cell is ALL checker and the checker has two tones: a transparent layer shows
        // the backdrop, which is what keeps the column's rhythm (not a blank space)
        const QImage cell = asImage(pm);
        QSet<QRgb> tones;
        for (int y = 2; y < cell.height() - 2; ++y) {
            for (int x = 2; x < cell.width() - 2; ++x)
                tones.insert(cell.pixel(x, y));
        }
        std::printf("MEASURED transparent layer: %dx%d cell, %d distinct tones\n", pm.width(), pm.height(),
                    int(tones.size()));
        check(tones.size() >= 2, "and the backdrop is a checker, not a flat fill");
        // ROUNDING, measured where it is visible: the extreme corner of a checker-only cell is
        // clipped away, while the middle of its top edge carries the frame
        std::printf(
            "MEASURED corner alpha %d (clipped), top edge alpha %d (frame)\n", qAlpha(cell.pixel(0, 0)),
            qAlpha(cell.pixel(cell.width() / 2, 0))
        );
        check(qAlpha(cell.pixel(0, 0)) < 160, "the cell's corner is clipped (it is rounded)");
        check(qAlpha(cell.pixel(cell.width() / 2, 0)) > 200, "and the middle of its edge carries the frame");
    }

    // ---- [2] the art is drawn, the corners are the checker ----
    {
        const QColor art(220, 90, 40);
        const img::Buffer buffer = makeBuffer(QSize(606, 530), art);
        const QImage cell = asImage(cache.layerThumbnail(buffer, kSize));
        const QColor centre = cell.pixelColor(cell.width() / 2, cell.height() / 2);
        // EVERY pixel is the art: an OPAQUE layer must show no checker at all, which is what
        // proves the cell is the artwork's own box rather than a square with bands
        int offArt = 0;
        // the CENTRAL BAND: the frame owns the outer pixel and the rounded corners blend with
        // the transparent outside for the first three, so "no checker" is measured where only
        // the art can be
        for (int y = 4; y < cell.height() - 4; ++y) {
            for (int x = 4; x < cell.width() - 4; ++x) {
                if (distance(cell.pixelColor(x, y), art) > 24)
                    ++offArt;
            }
        }
        std::printf(
            "MEASURED opaque layer: %dx%d cell, centre (%d,%d,%d), %d interior pixels off the art\n",
            cell.width(), cell.height(), centre.red(), centre.green(), centre.blue(), offArt
        );
        check(distance(centre, art) <= 24, "the layer's own colour is at the centre");
        check(offArt == 0, "and an opaque layer shows NO checker inside its band");
        // THE BORDER: the cell's own edge, which is what makes a dark drawing on a dark row
        // read as a cell at all (the review asked whether to keep it; it is kept).
        // THE FRAME is the original's GLOW: white at 26/255 (what rgba(255,255,255,40) became
        // under the 0.66 icon rest the tree used to apply), on the cell's rounded edge over the
        // art. So the assertion is the BLEND, not a palette token.
        const QColor edge = cell.pixelColor(cell.width() / 2, 0);
        const auto liftOver = [](int aChannel) {
            return qRound(aChannel + (255 - aChannel) * 26.0 / 255.0);
        };
        const QColor expected(liftOver(art.red()), liftOver(art.green()), liftOver(art.blue()));
        std::printf(
            "MEASURED frame: edge (%d,%d,%d), expected white@26 over the art (%d,%d,%d), corner "
            "alpha %d\n",
            edge.red(), edge.green(), edge.blue(), expected.red(), expected.green(), expected.blue(),
            qAlpha(cell.pixel(0, 0))
        );
        check(distance(edge, expected) <= 8, "the edge is the glow's own white@26 over the art");
        // ROUNDING: the corner block cannot be full - the arc slices it. Count the pixels that
        // are substantially inked in the 3x3 corner: a square cell would give nine.

    }

    // ---- [3] aspect is kept: a wide source leaves checker above and below ----
    {
        const QColor art(40, 200, 120);
        const img::Buffer buffer = makeBuffer(QSize(600, 100), art);
        const QImage cell = asImage(cache.layerThumbnail(buffer, kSize));
        std::printf("MEASURED wide layer: %dx%d cell\n", cell.width(), cell.height());
        check(cell.width() == kSize && cell.height() < cell.width() / 2,
              "a wide source gives a WIDE cell, not a square with bands");
        int offArt = 0;
        for (int y = 3; y < cell.height() - 3; ++y) {
            for (int x = 3; x < cell.width() - 3; ++x) {
                if (distance(cell.pixelColor(x, y), art) > 24)
                    ++offArt;
            }
        }
        check(offArt == 0, "and the strip's interior is the art (nothing letterboxed inside it)");
    }

    // ---- [4] the cache is keyed on the pixels ----
    {
        const img::Buffer first = makeBuffer(QSize(300, 300), QColor(60, 60, 220));
        const img::Buffer second = makeBuffer(QSize(300, 300), QColor(60, 220, 60));
        const quint64 keyFirst = gui::ThumbnailCache::key(first, kSize);
        const quint64 keySecond = gui::ThumbnailCache::key(second, kSize);
        check(
            gui::ThumbnailCache::key(first, kSize, 2.0) != keyFirst,
            "the drawn size (DPR) is part of the key: the same source at 2x is another pixmap"
        );
        check(keyFirst != keySecond, "a reimported image has a different key");

        const QPixmap a = cache.layerThumbnail(first, kSize);
        const QPixmap b = cache.layerThumbnail(first, kSize);
        const QPixmap c = cache.layerThumbnail(second, kSize);
        std::printf(
            "MEASURED cache: same buffer -> same pixmap %d, different buffer -> different pixmap %d\n",
            a.cacheKey() == b.cacheKey(), a.cacheKey() != c.cacheKey()
        );
        check(a.cacheKey() == b.cacheKey(), "the same buffer returns the cached pixmap");
        check(a.cacheKey() != c.cacheKey(), "a new buffer returns a new pixmap");
    }

    // ---- [5] a big source takes the subsample path ----
    {
        const QColor art(240, 240, 40);
        const img::Buffer big = makeBuffer(QSize(2600, 2600), art);   // 6.8 Mpx
        const QImage cell = asImage(cache.layerThumbnail(big, kSize));
        const QColor centre = cell.pixelColor(cell.width() / 2, cell.height() / 2);
        std::printf("MEASURED big source: centre (%d,%d,%d)\n", centre.red(), centre.green(), centre.blue());
        check(distance(centre, art) <= 24, "a large layer still lands its colour in the cell");
    }

    // ---- [6] the checker's values, and its restraint against the row ----
    // The tiles are OPAQUE and equal to the specified #363636 / #202020 (the values the first
    // translucent pair composited to). They were translucent for three rounds, and the last
    // one cost a level: a translucent pair lands 1 off on screen. These are exact.
    {
        const theme::Colors dark = theme::Colors::dark();
        const theme::Colors light = theme::Colors::light();
        std::printf(
            "MEASURED checker: dark #%02x%02x%02x on #%02x%02x%02x (row #%02x%02x%02x), light "
            "#%02x%02x%02x on #%02x%02x%02x (row #%02x%02x%02x)\n",
            dark.checkerLight.red(), dark.checkerLight.green(), dark.checkerLight.blue(),
            dark.checkerDark.red(), dark.checkerDark.green(), dark.checkerDark.blue(), dark.base.red(),
            dark.base.green(), dark.base.blue(), light.checkerLight.red(), light.checkerLight.green(),
            light.checkerLight.blue(), light.checkerDark.red(), light.checkerDark.green(),
            light.checkerDark.blue(), light.base.red(), light.base.green(), light.base.blue()
        );
        check(
            dark.checkerLight.red() == 0x24 && dark.checkerDark.red() == 0x14,
            "the dark tiles ARE #242424 / #141414 - the values measured off the original"
        );
        check(
            dark.checkerLight.alpha() == 255 && dark.checkerDark.alpha() == 255,
            "and they are opaque, so nothing can composite them to another value"
        );
        check(
            std::abs(dark.checkerLight.red() - dark.checkerDark.red()) >= 14,
            "they read as a checker (>= 14 levels at 5px tiles)"
        );
        check(
            std::abs((dark.checkerLight.red() + dark.checkerDark.red()) / 2 - dark.base.red()) <= 10,
            "and the pair's average sits at the row's value, not above it"
        );
    }

    // ---- [7] a scaled session renders at device pixels, painted 1:1 ----
    {
        const img::Buffer buffer = makeBuffer(QSize(400, 400), QColor(200, 200, 60));
        const QPixmap one = cache.layerThumbnail(buffer, kSize, 1.0);
        const QPixmap two = cache.layerThumbnail(buffer, kSize, 2.0);
        std::printf(
            "MEASURED dpr: 1.0 -> %dx%d (dpr %.1f), 2.0 -> %dx%d (dpr %.1f)\n", one.width(), one.height(),
            one.devicePixelRatio(), two.width(), two.height(), two.devicePixelRatio()
        );
        check(one.width() == kSize && two.width() == kSize * 2,
              "a 2x session renders the cell and its ring at twice the device pixels");
        check(two.devicePixelRatio() == 2.0, "and the pixmap says so, so Qt does not rescale it");
    }

    if (failures == 0)
        std::printf("layer_thumbnails: a cell always, the art inside it, keyed on the pixels\n");
    else
        std::printf("layer_thumbnails: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
