#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include "gui/ThumbnailCache.h"
#include "gui/theme/Colors.h"

namespace gui {

namespace {

// The buffer's pixels as a QImage that SHARES them: no copy, which is what makes a
// cache miss cost one downscale rather than a copy plus a downscale.
QImage viewOf(const img::Buffer& aImage) {
    const QSize size = aImage.pixelSize();
    if (size.isEmpty() || !aImage.data())
        return QImage();

    switch (aImage.format()) {
    case img::Format_RGBA8:
        return QImage(aImage.data(), size.width(), size.height(), QImage::Format_RGBA8888);
    case img::Format_RGB8:
        return QImage(aImage.data(), size.width(), size.height(), QImage::Format_RGB888);
    case img::Format_A8:
        return QImage(aImage.data(), size.width(), size.height(), QImage::Format_Grayscale8);
    default:
        return QImage();
    }
}

} // namespace

QImage ThumbnailCache::toImage(const img::Buffer& aImage) { return viewOf(aImage); }

quint64 ThumbnailCache::key(const img::Buffer& aImage, int aSize, qreal aDpr) {
    // A REIMPORT REPLACES THE BUFFER, so the data pointer and the size together identify
    // the pixels: nothing else about a resource image changes in place (the app uploads
    // the texture from this same buffer, and a reload hands over a new block). The drawn
    // size is part of the key because the same source at another DPR is another pixmap.
    quint64 h = reinterpret_cast<quint64>(aImage.data());
    h ^= quint64(aImage.size()) * 0x9E3779B97F4A7C15ull;
    h ^= quint64(aImage.pixelSize().width()) << 21;
    h ^= quint64(aImage.pixelSize().height()) << 42;
    h ^= quint64(aSize) * 0xBF58476D1CE4E5B9ull;
    h ^= quint64(aImage.format()) * 0x94D049BB133111EBull;
    h ^= quint64(qRound(aDpr * 1000.0)) * 0xC2B2AE3D27D4EB4Full;
    return h;
}

QSize ThumbnailCache::fittedSize(const img::Buffer& aImage, int aSize) {
    const QSize size = aImage.pixelSize();
    if (size.isEmpty())
        return QSize();
    const QSize target = size.scaled(QSize(aSize, aSize), Qt::KeepAspectRatio);
    return QSize(std::max(2, target.width()), std::max(2, target.height()));
}

QPixmap ThumbnailCache::layerThumbnail(const img::Buffer& aImage, int aSize, qreal aDpr) {
    const qreal dpr = aDpr > 0.0 ? aDpr : 1.0;
    const quint64 k = key(aImage, aSize, dpr);
    const auto found = mCache.constFind(k);
    if (found != mCache.constEnd())
        return found.value();

    QImage image = toImage(aImage);
    if (image.isNull())
        return QPixmap();

    const int box = std::max(2, qRound(aSize * dpr));
    // the artwork's own aspect inside the box, in device pixels, at least 2px on each side
    const QSize target = image.size().scaled(QSize(box, box), Qt::KeepAspectRatio);
    const int w = std::max(2, target.width());
    const int h = std::max(2, target.height());

    // SUBSAMPLE FIRST when the source is big. Measured on a 16.9 Mpx layer: 75.7 ms
    // direct, 2.33 ms after a 1/8 fast pass, and the result is indistinguishable at this
    // size - so the halving runs until the source is small enough to smooth cheaply.
    while (qint64(image.width()) * image.height() > kSubsamplePixels) {
        image = image.scaled(
            std::max(1, image.width() / 2), std::max(1, image.height() / 2), Qt::IgnoreAspectRatio,
            Qt::FastTransformation
        );
    }
    if (image.width() != w || image.height() != h)
        image = image.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    // THE CELL: a ROUNDED, clipped field with the alpha checker under the art, and the frame
    // drawn OVER its own edge - the arrangement the review preferred (a ring outside the art
    // read as a second border around the row's slot rather than as the image's edge).
    const theme::Colors& colors = theme::Colors::current();
    const int square = std::max(2, qRound(double(kCheckerSquare) * dpr));
    const qreal radius = std::min(qreal(std::min(w, h)) / 2.0, double(kCornerRadius) * dpr);

    QImage cell(w, h, QImage::Format_ARGB32_Premultiplied);
    cell.fill(Qt::transparent);
    {
        QPainter painter(&cell);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // the checker, clipped to the rounded cell
        QPainterPath rounded;
        rounded.addRoundedRect(QRectF(0, 0, w, h), radius, radius);
        painter.setClipPath(rounded);
        painter.fillRect(QRectF(0, 0, w, h), colors.checkerLight);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.checkerDark);
        for (int y = 0; y < h; y += square) {
            for (int x = 0; x < w; x += square) {
                if (((x / square) + (y / square)) % 2 == 0)
                    painter.drawRect(x, y, square, square);
            }
        }

        // the art, on top of the checker
        painter.drawImage(0, 0, image);

        // the frame OVER that edge: the field-border token, on the rounded path, so it lands
        // on the checker/art rather than outside the cell. The path is INSET by half the pen
        // width, because a 1px stroke centred on the cell's edge only half-covers the outer
        // pixel - measured as an edge reading (142,77,52), i.e. art with a hint of frame,
        // which is the same invisibility the ring outside was meant to fix.
        painter.setClipping(false);
        painter.setBrush(Qt::NoBrush);
        // THE GLOW, reproduced rather than guessed: the original drew this frame in
        // rgba(255,255,255,40) and the tree then multiplied every icon by its 0.66 rest, so
        // what reached the screen was white at 26/255. The thumbnail is drawn at full opacity
        // now, so the same look is alpha 26 written directly.
        QPen pen(QColor(255, 255, 255, 26), 1);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        QPainterPath edge;
        edge.addRoundedRect(QRectF(0.5, 0.5, w - 1.0, h - 1.0), radius, radius);
        painter.drawPath(edge);
        painter.end();
    }

    QPixmap pixmap = QPixmap::fromImage(cell);
    // the pixmap IS device pixels; this is what makes Qt paint it 1:1 instead of scaling
    // a 22px pixmap down to the 16px an unstyled QTreeWidget asks for
    pixmap.setDevicePixelRatio(dpr);
    mCache.insert(k, pixmap);
    return pixmap;
}

} // namespace gui
