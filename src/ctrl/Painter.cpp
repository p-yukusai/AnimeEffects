#include "ctrl/Painter.h"
#include <QImage>
#include "XC.h"
#include "gl/Global.h"
#include "gl/Util.h"

namespace ctrl {

#if USE_GL_CORE_PROFILE
namespace {
// The drawer's slot is the batch size for every primitive it schedules. The
// default 512 is right for strokes; a 9000-point cloud (the point primitive's
// own gate) fits two slots, and eight slots of two vertex buffers is ~1 MB,
// allocated once. The weight field is not a point cloud: it is one indexed
// triangle draw per mesh through drawWeightField, below.
constexpr int kOverlayVtxPerSlot = 8192;
} // namespace

//-------------------------------------------------------------------------------------------------
GLCorePaintEngine::GLCorePaintEngine():
    QPaintEngine(
        QPaintEngine::AlphaBlend | QPaintEngine::Antialiasing | QPaintEngine::ConstantOpacity |
        QPaintEngine::PrimitiveTransform
    ),
    mDrawer(kOverlayVtxPerSlot),
    mFontDrawer(),
    mTextureCaches(),
    mTextCaches() {}

bool GLCorePaintEngine::begin(QPaintDevice* aDevPtr) {
    // qDebug() << "begin";
    (void)aDevPtr;
    mTextureCaches.reduceByStorageSize();
    mTextCaches.reduceByStorageSize();
    mDrawer.begin();

    // sync state
    auto painter = this->painter();
    if (painter) {
        syncBrush(painter->brush());
        syncPen(painter->pen());
        syncRenderHints(painter->renderHints());
    }

    return true;
}

bool GLCorePaintEngine::end() {
    mDrawer.end();
    mTextureCaches.updateStorageSize();
    mTextCaches.updateStorageSize();
    return true;
}

void GLCorePaintEngine::syncBrush(const QBrush& aBrush) {
    mDrawer.setBrushEnable(aBrush.style() != Qt::NoBrush);
    mDrawer.setBrush(aBrush.color());
}

void GLCorePaintEngine::syncPen(const QPen& aPen) {
    bool hasPen = true;
    gl::PrimitiveDrawer::PenStyle style;

    switch (aPen.style()) {
    case Qt::SolidLine:
        style = gl::PrimitiveDrawer::PenStyle_Solid;
        break;
    case Qt::DotLine:
        style = gl::PrimitiveDrawer::PenStyle_Dot;
        break;
    case Qt::DashLine:
    case Qt::DashDotLine:
    case Qt::DashDotDotLine:
    case Qt::CustomDashLine:
        style = gl::PrimitiveDrawer::PenStyle_Dash;
        break;
    default:
        hasPen = false; // Qt::NoPen will come here.
        style = gl::PrimitiveDrawer::PenStyle_Solid;
        break;
    }
    mDrawer.setPenEnable(hasPen);
    mDrawer.setPen(aPen.color(), aPen.widthF(), style);
}

void GLCorePaintEngine::syncRenderHints(QPainter::RenderHints aHints) {
    const bool useAA = aHints & QPainter::Antialiasing;
    mDrawer.setAntiAliasing(useAA);
}

void GLCorePaintEngine::updateState(const QPaintEngineState& aState) {
    if (aState.state() & QPaintEngine::DirtyBrush) {
        syncBrush(aState.brush());
    }

    if (aState.state() & QPaintEngine::DirtyPen) {
        syncPen(aState.pen());
    }

    if (aState.state() & QPaintEngine::DirtyHints) {
        syncRenderHints(aState.renderHints());
    }
}

void GLCorePaintEngine::drawPoints(const QPoint* aPoints, int aCount) {
    QVector<QPointF> points;
    points.reserve(aCount);
    for (int i = 0; i < aCount; ++i)
        points.append(QPointF(aPoints[i]));
    mDrawer.drawPoints(points.constData(), int(points.size()));
}

void GLCorePaintEngine::drawPoints(const QPointF* aPoints, int aCount) {
    // One command per slot, not one tiny ellipse per point: a heatmap's point
    // cloud is thousands of samples and the per-point path could not carry it.
    mDrawer.drawPoints(aPoints, aCount);
}

void GLCorePaintEngine::drawLines(const QLine* aLines, int aCount) {
    for (int i = 0; i < aCount; ++i)
        mDrawer.drawLine(QLineF(aLines[i]));
}

void GLCorePaintEngine::drawLines(const QLineF* aLines, int aCount) {
    for (int i = 0; i < aCount; ++i)
        mDrawer.drawLine(aLines[i]);
}

void GLCorePaintEngine::drawRects(const QRect* aRects, int aCount) {
    for (int i = 0; i < aCount; ++i)
        mDrawer.drawRect(QRectF(aRects[i]));
}

void GLCorePaintEngine::drawRects(const QRectF* aRects, int aCount) {
    for (int i = 0; i < aCount; ++i)
        mDrawer.drawRect(aRects[i]);
}

void GLCorePaintEngine::drawEllipse(const QRectF& aRect) {
    mDrawer.drawEllipse(aRect.center(), 0.5f * aRect.width(), 0.5f * aRect.height());
}

void GLCorePaintEngine::drawPath(const QPainterPath& aPath) { (void)aPath; }

void GLCorePaintEngine::drawPolygon(const QPoint* aPoints, int aCount, PolygonDrawMode aMode) {
    switch (aMode) {
    case QPaintEngine::ConvexMode:
        mDrawer.drawConvexPolygon(aPoints, aCount);
        break;
    case QPaintEngine::PolylineMode:
        mDrawer.drawPolyline(aPoints, aCount);
        break;
    default:
        mDrawer.drawPolygon(aPoints, aCount);
        break;
    }
}

void GLCorePaintEngine::drawPolygon(const QPointF* aPoints, int aCount, PolygonDrawMode aMode) {
    switch (aMode) {
    case QPaintEngine::ConvexMode:
        mDrawer.drawConvexPolygon(aPoints, aCount);
        break;
    case QPaintEngine::PolylineMode:
        mDrawer.drawPolyline(aPoints, aCount);
        break;
    default:
        mDrawer.drawPolygon(aPoints, aCount);
        break;
    }
}

void GLCorePaintEngine::drawImage(
    const QRectF& aRect, const QImage& aImage, const QRectF& aSrcRect, Qt::ImageConversionFlags aFlags
) {
    (void)aFlags;
    const QImage* ptr = &aImage;
    const qint64 key = aImage.cacheKey();

    auto texture = mTextureCaches.get(key, [=]() {
        auto cache = new TextureCaches::Cache();
        // VERTICAL ONLY. The drawer's quad maps the texture's v axis
        // bottom-up, so the vertical flip is what makes a QImage's top row
        // land at the top. Flipping horizontally as well mirrored every image
        // (the weight heatmap's wash came out 180-degree wrong in x; the icons
        // were mirrored too, which was easy to miss).
        cache->obj.reset(new QOpenGLTexture(ptr->flipped(Qt::Vertical)));
        cache->key = key;
        cache->size = (size_t)ptr->sizeInBytes();

        cache->obj->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
        cache->obj->setMagnificationFilter(QOpenGLTexture::Linear);

        return cache;
    });
    mDrawer.drawTexture(aRect, texture->textureId(), QSize(texture->width(), texture->height()), aSrcRect);
}

void GLCorePaintEngine::drawPixmap(const QRectF& aRect, const QPixmap& aPixmap, const QRectF& aSrcRect) {
    if (aPixmap.paintEngine()->type() == QPaintEngine::Raster && !aPixmap.isQBitmap()) {
        const QPixmap* ptr = &aPixmap;
        const qint64 key = aPixmap.cacheKey();

        auto texture = mTextureCaches.get(key, [=]() {
            auto cache = new TextureCaches::Cache();
            auto image = ptr->toImage();
            cache->obj.reset(new QOpenGLTexture(image.flipped(Qt::Vertical)));
            cache->key = key;
            cache->size = (size_t)image.sizeInBytes();

            cache->obj->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
            cache->obj->setMagnificationFilter(QOpenGLTexture::Linear);

            return cache;
        });
        mDrawer.drawTexture(aRect, texture->textureId(), QSize(texture->width(), texture->height()), aSrcRect);
    }
}

void GLCorePaintEngine::drawTextItem(const QPointF& aPos, const QTextItem& aTextItem) {
    mDrawer.end();
    auto key = gl::TextObject::getMapKey(aTextItem.text());

    auto textObj = mTextCaches.get(key, [&]() {
        auto obj = new gl::TextObject(aTextItem.text());

        this->mFontDrawer.setColor(QColor(255, 255, 255, 255));
        this->mFontDrawer.draw(aTextItem.font(), *obj);

        auto cache = new TextCaches::Cache();
        cache->obj.reset(obj);
        cache->key = key;
        cache->size = (size_t)obj->pixelCount();
        return cache;
    });
    mDrawer.begin();
    auto pos = aPos.toPoint() + QPoint(0.0f, -aTextItem.ascent());
    mDrawer.drawTexture(QRect(pos, textObj->texture().size()), textObj->texture());
}

//-------------------------------------------------------------------------------------------------
// THE AUTHORED-WEIGHT FIELD. A tiny program of its own, because this is not one of the
// QPainter primitives the drawer batches: it is an indexed triangle draw of the mesh
// itself, one weight per vertex. The GPU interpolates the weight across the triangle and
// the fragment shader evaluates the ramp - a vertex is a control value, the surface between
// vertices is the field. No image, no readback, no resolution.
void GLCorePaintEngine::ensureWeightFieldProgram() {
    if (mWeightFieldReady)
        return;

    static const char* const kVertexSource = R"(#version 330
in vec3 inPosition;
in float inWeight;
uniform mat4 uModelView;
out float vWeight;
void main() {
    gl_Position = uModelView * vec4(inPosition, 1.0);
    vWeight = inWeight;
}
)";
    // The ramp is the heat ramp the state colours and the plan's figures use: cool = weak,
    // hot = strong. KEEP the alpha and the threshold in step with
    // GLCorePaintEngine::kWeightFieldMinWeight (GLSL cannot read the C++ constant).
    static const char* const kFragmentSource = R"(#version 330
in float vWeight;
out vec4 oColor;

vec3 heatRamp(float t) {
    if (t <= 0.25) return mix(vec3(0.156863, 0.274510, 1.0), vec3(0.0, 0.862745, 1.0), t / 0.25);
    if (t <= 0.50) return mix(vec3(0.0, 0.862745, 1.0), vec3(0.0, 0.901961, 0.313725), (t - 0.25) / 0.25);
    if (t <= 0.75) return mix(vec3(0.0, 0.901961, 0.313725), vec3(1.0, 0.862745, 0.0), (t - 0.50) / 0.25);
    return mix(vec3(1.0, 0.862745, 0.0), vec3(1.0, 0.156863, 0.156863), (t - 0.75) / 0.25);
}

void main() {
    // Not part of the field: transparent, never a faint wash over the whole layer.
    if (vWeight < 0.002) discard;
    // The wash has to read over artwork of any colour without hiding it; the skeleton and
    // the brush stay legible above it.
    oColor = vec4(heatRamp(clamp(vWeight, 0.0, 1.0)), 120.0 / 255.0);
}
)";

    if (!mWeightFieldProgram.setVertexSource(QString::fromLatin1(kVertexSource))
        || !mWeightFieldProgram.setFragmentSource(QString::fromLatin1(kFragmentSource))
        || !mWeightFieldProgram.link()) {
        XC_FATAL_ERROR(
            "OpenGL Error", "Failed to build the weight-field shader.",
            mWeightFieldProgram.log()
        );
    }
    mWeightFieldReady = true;
}

void GLCorePaintEngine::drawWeightField(const WeightField& aField) {
    if (!aField.positions || !aField.weights || aField.vertexCount <= 0 || !aField.indices
        || aField.indexCount < 3) {
        return;
    }
    ensureWeightFieldProgram();

    gl::Global::Functions& ggl = gl::Global::functions();

    // OVER the artwork: no depth test, source-over with the destination kept opaque (the
    // same blend the ink pass uses, so a translucent wash never punches a hole through the
    // window). MSAA is on because the widget's framebuffer is multisampled and a surface's
    // silhouette is a pixel event like any other; the drawer re-applies its own AA state.
    gl::Util::resetRenderState();
    ggl.glEnable(GL_BLEND);
    ggl.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    ggl.glEnable(GL_MULTISAMPLE);

    mWeightFieldProgram.bind();
    mWeightFieldProgram.setAttributeArray("inPosition", aField.positions, aField.vertexCount);
    mWeightFieldProgram.setAttributeArray("inWeight", aField.weights, aField.vertexCount);
    mWeightFieldProgram.setUniformValue("uModelView", aField.modelView);

    aField.indices->bind();
    ggl.glDrawElements(GL_TRIANGLES, aField.indexCount, GL_UNSIGNED_INT, nullptr);
    aField.indices->release();
    mWeightFieldProgram.release();

    GL_CHECK_ERROR();
}

//-------------------------------------------------------------------------------------------------
int GLCorePaintDevice::metric(PaintDeviceMetric aMetric) const {
    switch (aMetric) {
    case QPaintDevice::PdmWidth:
        return mOrigin.width();
    case QPaintDevice::PdmHeight:
        return mOrigin.height();
    case QPaintDevice::PdmWidthMM:
        return mOrigin.widthMM();
    case QPaintDevice::PdmHeightMM:
        return mOrigin.heightMM();
    case QPaintDevice::PdmNumColors:
        return mOrigin.colorCount();
    case QPaintDevice::PdmDepth:
        return mOrigin.depth();
    case QPaintDevice::PdmDpiX:
        return mOrigin.logicalDpiX();
    case QPaintDevice::PdmDpiY:
        return mOrigin.logicalDpiY();
    case QPaintDevice::PdmPhysicalDpiX:
        return mOrigin.physicalDpiX();
    case QPaintDevice::PdmPhysicalDpiY:
        return mOrigin.physicalDpiY();
    case QPaintDevice::PdmDevicePixelRatio:
        return mOrigin.devicePixelRatio();
    case QPaintDevice::PdmDevicePixelRatioScaled:
        return mOrigin.devicePixelRatioFScale();
    default:
        XC_ASSERT(0);
        return 0;
    }
}

//-------------------------------------------------------------------------------------------------
PainterHandle::PainterHandle(): mEngine(), mDevice(), mPainter() {}

QPainter* PainterHandle::begin(QPaintDevice& aDevice) {
    QMatrix4x4 view;
    view.ortho(0.0f, aDevice.width(), aDevice.height(), 0.0f, -1.0f, 1.0f);
    mEngine.setViewMatrix(view);

    mDevice.reset(new GLCorePaintDevice(aDevice, mEngine));
    mPainter.reset(new QPainter(mDevice.data()));
    return mPainter.data();
}

void PainterHandle::end() {
    mPainter->end();
    mPainter.reset();
    mDevice.reset();
}
#else // USE_GL_CORE_PROFILE
PainterHandle::PainterHandle(): mPainter() {}

QPainter* PainterHandle::begin(QPaintDevice& aDevice) {
    // QPainter use legacy gl commands.
    mPainter.reset(new QPainter(&aDevice));
    return mPainter.data();
}

void PainterHandle::end() {
    mPainter->end();
    mPainter.reset();
}
#endif // USE_GL_CORE_PROFILE

} // namespace ctrl
