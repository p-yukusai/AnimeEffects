#ifndef CTRL_PAINTER_H
#define CTRL_PAINTER_H

#include <functional>
#include <QPainter>
#include <QRadialGradient>
#include <QPaintEngine>
#include <QPaintDevice>
#include <QOpenGLTexture>
#include "gl/PrimitiveDrawer.h"
#include "gl/FontDrawer.h"
#include "gl/TextObject.h"

namespace ctrl {

#ifdef USE_GL_CORE_PROFILE

template<typename tKey, typename tCacheObj>
class CacheHolder {
public:
    struct Cache {
        Cache(): obj(), size(), key(), used(false) {}
        QScopedPointer<tCacheObj> obj;
        size_t size;
        tKey key;
        bool used;
    };

    CacheHolder(): mCaches(), mUsedCacheLog(), mStorageSize() {}

    ~CacheHolder() { deleteAll(); }

    tCacheObj* get(tKey aKey, const std::function<Cache*()>& aCreator) {
        Cache*& cache = mCaches[aKey];
        if (!cache) {
            cache = aCreator();
            cache->used = true;
            mUsedCacheLog.push_front(cache);
            // qDebug() << "create" << cache;
        } else {
            cache->used = true;
            mUsedCacheLog.removeOne(cache);
            mUsedCacheLog.push_front(cache);
            // qDebug() << "found" << cache;
        }
        return cache->obj.data();
    }

    // call it on begining of the rendering
    void reduceByStorageSize() {
        size_t currentSize = 0;
        for (auto itr = mUsedCacheLog.begin(); itr != mUsedCacheLog.end(); ++itr) {
            auto usedCache = *itr;
            if (currentSize + usedCache->size > mStorageSize) {
                // delete it and after
                while (itr != mUsedCacheLog.end()) {
                    usedCache = *itr;
                    mCaches.remove(usedCache->key);
                    itr = mUsedCacheLog.erase(itr);
                    delete usedCache;
                    // qDebug() << "reduce" << usedCache;
                }
                break;
            }
        }
    }

    // call it on end of the rendering
    void updateStorageSize() {
        size_t usedSize = 0;
        for (auto& cache : mCaches) {
            if (cache->used) {
                usedSize += cache->size;
            }
            cache->used = false;
        }
        // qDebug() << "update size" << mStorageSize << usedSize;
        mStorageSize = std::max(mStorageSize, usedSize);
    }

    void deleteAll() {
        qDeleteAll(mCaches);
        mCaches.clear();
        mUsedCacheLog.clear();
    }

private:
    QMap<tKey, Cache*> mCaches;
    QList<Cache*> mUsedCacheLog;
    size_t mStorageSize;
};

//-------------------------------------------------------------------------------------------------
class GLCorePaintEngine: public QPaintEngine {
public:
    GLCorePaintEngine();

    void setViewMatrix(const QMatrix4x4& aMtx) { mDrawer.setViewMatrix(aMtx); }

    // THE BRUSH TIP: a disc whose alpha is ctrl::brushFalloff, evaluated per fragment by the
    // overlay's own shader. Not a QPainter primitive (no gradient, no path), and not a bitmap either
    // - a texture run out of resolution at 800px, which is what "the overlay breaks at a certain
    // size" was. Call it through ctrl::paintBrushTip, which also serves the raster path.
    void drawBrushTip(const QPointF& aCenter, float aRadius, float aHardness, const QColor& aInk) {
        mDrawer.drawBrushTip(aCenter, aRadius, aHardness, aInk);
    }

    virtual QPaintEngine::Type type() const { return QPaintEngine::OpenGL2; }

    virtual bool begin(QPaintDevice* aDevPtr);
    virtual bool end();

    virtual void updateState(const QPaintEngineState& aState);

    virtual void drawPoints(const QPoint* aPoints, int aCount);
    virtual void drawPoints(const QPointF* aPoints, int aCount);

    virtual void drawLines(const QLine* aLines, int aCount);
    virtual void drawLines(const QLineF* aLines, int aCount);

    virtual void drawRects(const QRect* aRects, int aCount);
    virtual void drawRects(const QRectF* aRects, int aCount);

    virtual void drawEllipse(const QRectF& aRect);

    virtual void drawPath(const QPainterPath& aPath);

    virtual void drawPolygon(const QPoint* aPoints, int aCount, PolygonDrawMode aMode);
    virtual void drawPolygon(const QPointF* aPoints, int aCount, PolygonDrawMode aMode);

    virtual void drawImage(
        const QRectF& aRect,
        const QImage& aImage,
        const QRectF& aSrcRect,
        Qt::ImageConversionFlags aFlags = Qt::AutoColor
    );

    virtual void drawPixmap(const QRectF& aRect, const QPixmap& aPixmap, const QRectF& aSrcRect);

    // virtual void drawTiledPixmap(const QRectF& aRect, const QPixmap& aPixmap, const QPointF& aPos);

    virtual void drawTextItem(const QPointF& aPos, const QTextItem& aTextItem);

private:
    typedef CacheHolder<qint64, QOpenGLTexture> TextureCaches;
    typedef CacheHolder<gl::TextObject::MapKey, gl::TextObject> TextCaches;
    void syncBrush(const QBrush& aBrush);
    void syncPen(const QPen& aPen);
    void syncRenderHints(QPainter::RenderHints aHints);

    gl::PrimitiveDrawer mDrawer;
    gl::FontDrawer mFontDrawer;
    TextureCaches mTextureCaches;
    TextCaches mTextCaches;
};

//-------------------------------------------------------------------------------------------------
class GLCorePaintDevice: public QPaintDevice {
public:
    GLCorePaintDevice(const QPaintDevice& aOriginDevice, GLCorePaintEngine& aEngine):
        mOrigin(aOriginDevice), mEngine(aEngine) {}
    virtual ~GLCorePaintDevice() {}
    virtual QPaintEngine* paintEngine() const { return &mEngine; }
    virtual int metric(PaintDeviceMetric aMetric) const;

private:
    const QPaintDevice& mOrigin;
    GLCorePaintEngine& mEngine;
};

//-------------------------------------------------------------------------------------------------
// ONE CALL FOR THE BRUSH TIP, WHATEVER THE RASTERISER IS UNDER THE PAINTER. The application paints
// the viewport through GLCorePaintEngine (a bounded quad and a falloff fragment shader); a raster
// QPainter (a test, a screenshot, a build without the GL overlay) gets the same picture from a radial
// gradient. The falloff is ctrl::brushFalloff in both cases, so the two cannot disagree about what
// hardness means.
inline void paintBrushTip(
    QPainter& aPainter, const QPointF& aCenter, float aRadius, float aHardness, const QColor& aInk
) {
    if (aRadius <= 0.5f)
        return;
    if (auto* engine = dynamic_cast<GLCorePaintEngine*>(aPainter.paintEngine())) {
        engine->drawBrushTip(aCenter, aRadius, aHardness, aInk);
        return;
    }
    // the raster path: the same profile as a gradient (Qt cannot express brushFalloff's flat core in
    // a QRadialGradient, so the core is a stop at radius*hardness and the band is the smoothstep's)
    QRadialGradient gradient(aCenter, aRadius);
    const float hardness = std::min(std::max(aHardness, 0.0f), 1.0f);
    gradient.setColorAt(0.0, aInk);
    gradient.setColorAt(hardness, aInk);
    gradient.setColorAt((hardness + 1.0f) * 0.5f, QColor(aInk.red(), aInk.green(), aInk.blue(), aInk.alpha() / 2));
    gradient.setColorAt(1.0, QColor(aInk.red(), aInk.green(), aInk.blue(), 0));
    aPainter.setPen(Qt::NoPen);
    aPainter.setBrush(gradient);
    aPainter.drawEllipse(aCenter, aRadius, aRadius);
}

class PainterHandle {
public:
    PainterHandle();
    QPainter* begin(QPaintDevice& aDevice);
    void end();

private:
    GLCorePaintEngine mEngine;
    QScopedPointer<GLCorePaintDevice> mDevice;
    QScopedPointer<QPainter> mPainter;
};

#else // USE_COREPROFILE_PAINTER
class PainterHandle {
public:
    PainterHandle();
    QPainter* begin(QPaintDevice& aDevice);
    void end();

private:
    QScopedPointer<QPainter> mPainter;
};
#endif // USE_COREPROFILE_PAINTER

} // namespace ctrl

#endif // CTRL_PAINTER_H
