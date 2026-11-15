// Contract test for the SUBSET of QPaintEngine that the viewport overlay's GL
// engine actually implements (ctrl::GLCorePaintEngine, src/ctrl/Painter.cpp).
//
// This is the GL sibling of the graph panel's pixel gate (tests/graph_panel_pixels.cpp,
// which replaced the deleted graph_render_pixels). This one paints through the real
// engine - ctrl::PainterHandle, the same object gui::MainDisplayWidget::paintEvent
// drives - into an offscreen framebuffer, and counts what arrived one probe at a
// time. The engines do not agree: a raster QPainter fills a QPainterPath, this one
// drops it silently, which is how a graph panel body and its curve polylines ended
// up invisible on screen while the raster painter test stayed green.
//
// Every count is a MEASUREMENT, printed as `MEASURED <probe> pixels=N`, so the
// subset this engine supports is readable from a log rather than only from
// pass/fail. The harness fails both when a probe that must paint counts 0 and
// when a probe that must not paint counts anything.
//
// How each count is taken:
//   * one pass per probe: bind this file's own FBO, clear it to kClear, run one
//     ctrl::PainterHandle::begin/end, then read the whole attachment back.
//     Nothing accumulates across probes, so no probe can bleed into another.
//   * each probe draws inside its own cell - a 100x150 rect of the 400x300
//     image, four across and two down - and only that rect is counted.
//   * GL's origin is bottom-left, so the readback is flipped vertically before a
//     single pixel is counted (see readFboTopDown): without the flip every count
//     would come from the mirrored rect, i.e. from the wrong cell.
//   * a pixel counts when the largest per-channel distance from the clear colour
//     exceeds kPaintedDistance. The clear colour is magenta, which none of the
//     colours drawn here is within that distance of, so "the pixel changed" can
//     only mean "something was drawn here".
//   * the first pass draws nothing at all (the "control" probe): if it counted
//     anything, the readback or the counting would be inventing pixels and every
//     other number here would be worthless.
//
// Nine probes, eight cells: the two text probes are the pair that must differ in
// exactly ONE variable (the scheduled brush) to pin the rule that ate the graph
// panel's axis labels, so they deliberately share the text cell - each in its
// own cleared pass, which is what makes them independent.
//
// The two engine rules pinned here, both of them silent in a build:
//   1 drawRoundedRect paints NOTHING: QPainter builds a QPainterPath for it and
//     calls drawPath, and GLCorePaintEngine::drawPath() is `{ (void)aPath; }`
//     (src/ctrl/Painter.cpp:127). Path support would have to be added to that
//     engine - and this harness updated - before a caller may use one.
//   2 drawText with no brush scheduled paints NOTHING: drawTextItem blits a
//     glyph texture, i.e. a `usePen = false` draw, and
//     gl::PrimitiveDrawer::pushDrawCommand returns early for those unless
//     mScheduledState.hasBrush is set. The same text with a brush paints.
//
// Headless and self-contained: a real core-profile QOpenGLWidget context (the
// bootstrap roundtrip_main.cpp uses), with all rendering done in this file's own
// FBO. The widget's paint event is never involved, no window manager is needed,
// and no .anie is read. Run it under xvfb-run:
//   xvfb-run -a build/tests/Debug/overlay_engine_primitives
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QOpenGLFunctions_4_0_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLWidget>
#include <QPaintEngine>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

#include "XCAssert.h"
#include "ctrl/Painter.h"
#include "gl/DeviceInfo.h"
#include "gl/Framebuffer.h"
#include "gl/Global.h"
#include "gl/Texture.h"
#include "gl/VertexArrayObject.h"

// The ctrl/core archives this target links reference the GUI
// AudioPlaybackWidget::deserialize (core::Project.cpp uses it for a .aemus
// sidecar). Nothing here ever loads a project, so stub it with the one-liner
// roundtrip_main.cpp uses and the target needs no gui library.
#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

namespace {

class HarnessAssertHandler: public XCAssertHandler {
public:
    // An engine invariant firing means this harness can no longer describe the
    // engine (a pending GL error, an unexpected state). Die loudly instead of
    // printing a count that would be a measurement of something else.
    void failure() const override { std::abort(); }
};

class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString& aText, const QString& aInfo, const QString& aDetail) const override {
        std::fprintf(stderr, "XC_FATAL %s %s %s\n", qPrintable(aText), qPrintable(aInfo), qPrintable(aDetail));
        std::abort();
    }
};

HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;

#define CHECK(cond)                                                                                     \
    do {                                                                                                \
        if (!(cond)) {                                                                                  \
            std::printf("FAIL %d: %s\n", __LINE__, #cond);                                              \
            return 1;                                                                                   \
        }                                                                                               \
    } while (false)

#define CHECK_MSG(cond, msg)                                                                            \
    do {                                                                                                \
        if (!(cond)) {                                                                                  \
            std::printf("FAIL %d: %s -- %s\n", __LINE__, #cond, msg);                                   \
            return 1;                                                                                   \
        }                                                                                               \
    } while (false)

constexpr int kWidth = 400;
constexpr int kHeight = 300;
constexpr int kCellWidth = 100;
constexpr int kCellHeight = 150;
constexpr int kCellsPerRow = kWidth / kCellWidth;

// "count the whole image", for the pass that draws nothing.
constexpr int kWholeImage = -1;

// The colour every probe pass is cleared to. Magenta on purpose: nothing drawn
// below is within the counting tolerance of it.
const QColor kClear(255, 0, 255);

// The single colour every probe paints with, so a probe's count is never
// ambiguous about which ink was counted.
const QColor kInk(0, 255, 0);

// A pixel counts when its largest per-channel distance from the clear colour
// exceeds this. Alpha is one of the channels: the engine blends with
// GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA, so it moves the destination alpha too and
// a draw that only moved alpha still painted.
constexpr int kPaintedDistance = 8;

bool isPainted(const uchar* aPixel) {
    return std::max(
               {std::abs(int(aPixel[0]) - kClear.red()),
                std::abs(int(aPixel[1]) - kClear.green()),
                std::abs(int(aPixel[2]) - kClear.blue()),
                std::abs(int(aPixel[3]) - kClear.alpha())}
           ) > kPaintedDistance;
}

// How many pixels of aRect the image has painted. The rect is clipped to the
// image, so a caller may hand in one that runs past the edge.
int paintedCount(const QImage& aImage, const QRect& aRect) {
    const QRect r = aRect.intersected(aImage.rect());
    int count = 0;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const uchar* line = aImage.constScanLine(y);
        for (int x = r.left(); x <= r.right(); ++x) {
            if (isPainted(line + 4 * x)) {
                ++count;
            }
        }
    }
    return count;
}

// The cell of a 400x300 image that probe `aCell` draws in and is counted over.
QRect cellRect(int aCell) {
    return QRect(
        (aCell % kCellsPerRow) * kCellWidth, (aCell / kCellsPerRow) * kCellHeight, kCellWidth, kCellHeight
    );
}

// Read the whole FBO back as a TOP-DOWN RGBA image, with the FBO already bound.
// GL's origin is bottom-left, so glReadPixels hands back the bottom row first:
// the buffer has to be flipped vertically before anything is counted, or every
// count would be taken from the mirrored rect - i.e. from the wrong cell.
QImage readFboTopDown() {
    QImage image(kWidth, kHeight, QImage::Format_RGBA8888);
    gl::Global::functions().glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
    return image.flipped(Qt::Vertical);
}

struct Probe {
    const char* name;
    const char* note; // what the count means; printed with a failure
    int cell;         // kWholeImage, or a 100x150 cell index
    bool expectPainted;
    std::function<void(QPainter&, const QPoint&)> draw; // (painter, cell origin)
    // A painted probe may also require at least this many pixels: a single
    // 10px point must be a disc, not the 1px a GL_POINTS draw falls back to
    // when glPointSize is not applied.
    int minPixels = 0;
};

// One weight-field quad at `aOrigin`: a 60x60 square whose weight rises left to right.
// The geometry is the harness's own, in the scene coordinates the PainterHandle's view
// matrix uses; the model-view is the same ortho, so the quad lands where it is asked to.
void drawWeightFieldQuad(QPainter& aPainter, const QPoint& aOrigin, float aLeft, float aRight) {
    auto* engine = dynamic_cast<ctrl::GLCorePaintEngine*>(aPainter.paintEngine());
    if (!engine) {
        std::fprintf(stderr, "FAIL: the weight field was asked to draw without the GL engine\n");
        std::abort();
    }

    gl::BufferObject indices(GL_ELEMENT_ARRAY_BUFFER);
    const GLuint indexData[6] = {0, 1, 2, 0, 2, 3};
    indices.resetData(6, GL_STATIC_DRAW, indexData);

    const float x0 = aOrigin.x() + 20.0f;
    const float y0 = aOrigin.y() + 45.0f;
    const float x1 = x0 + 60.0f;
    const float y1 = y0 + 60.0f;
    const gl::Vector3 positions[4] = {
        gl::Vector3::make(x0, y0, 0.0f),
        gl::Vector3::make(x1, y0, 0.0f),
        gl::Vector3::make(x1, y1, 0.0f),
        gl::Vector3::make(x0, y1, 0.0f),
    };
    const float weights[4] = {aLeft, aRight, aRight, aLeft};

    QMatrix4x4 modelView;
    modelView.ortho(0.0f, kWidth, kHeight, 0.0f, -1.0f, 1.0f);

    ctrl::GLCorePaintEngine::WeightField field;
    field.positions = positions;
    field.weights = weights;
    field.vertexCount = 4;
    field.indices = &indices;
    field.indexCount = 6;
    field.modelView = modelView;
    engine->drawWeightField(field);
}

const uchar* pixelAt(const QImage& aImage, int aX, int aY) {
    return aImage.constScanLine(aY) + 4 * aX;
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Live core-profile context, exactly as roundtrip_main.cpp takes one. The
    // format has to be the default before the widget creates its context, which
    // happens on the first show()/makeCurrent() below.
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QOpenGLWidget glWidget;
    glWidget.resize(kWidth, kHeight);
    glWidget.show();
    glWidget.makeCurrent();
    if (!glWidget.context()) {
        std::fprintf(stderr, "FAIL: no GL context could be created\n");
        return 2;
    }
    auto funcs = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(glWidget.context());
    if (!funcs || !funcs->initializeOpenGLFunctions()) {
        std::fprintf(stderr, "FAIL: no GL function table for the widget's context\n");
        return 2;
    }
    gl::Global::setContext(glWidget);
    gl::Global::setFunctions(*funcs);

    gl::DeviceInfo dev;
    dev.vender = "harness";
    dev.renderer = "harness";
    dev.version = "5";
    dev.maxTextureSize = 16384;
    dev.maxRenderBufferSize = 16384;
    gl::DeviceInfo::setInstance(&dev);

    // The engine's view matrix comes from the device's size (PainterHandle::begin
    // reads the widget it is handed), and every cell below is counted in those
    // coordinates: a window manager that resized this widget would silently move
    // every count, so the size is asserted rather than assumed.
    CHECK_MSG(
        glWidget.width() == kWidth && glWidget.height() == kHeight,
        "the widget must stay 400x300: the engine's view matrix and every cell rect use its size"
    );

    std::printf(
        "INFO GL renderer=%s version=%s\n",
        reinterpret_cast<const char*>(funcs->glGetString(GL_RENDERER)),
        reinterpret_cast<const char*>(funcs->glGetString(GL_VERSION))
    );

    // Drop anything the context, the widget or their setup left pending:
    // GL_CHECK_ERROR (gl/Global.h) turns a *pending* error into Q_ASSERT(0), so a
    // stale one would abort on the first probe and read as that probe's fault.
    {
        GLuint e = 0;
        while ((e = funcs->glGetError()) != GL_NO_ERROR) {
        }
    }

    // A core-profile draw with NO vertex array object bound is
    // GL_INVALID_OPERATION, and the engine's GL_CHECK_ERROR() turns that into
    // Q_ASSERT(0) - an abort in a Debug build. gl::PrimitiveDrawer never binds
    // one itself: gui::MainDisplayWidget::initializeGL() creates mDefaultVAO and
    // leaves it bound for the whole life of the overlay, and every draw below
    // depends on that here too.
    gl::VertexArrayObject vao;
    vao.bind();

    // Render into this harness's OWN framebuffer, never the widget's:
    // QOpenGLWidget::grabFramebuffer() re-enters Qt's render for the widget and
    // (with the default NoPartialUpdate) invalidates the colour buffer first, so
    // a per-probe readback through it returned pixels for one probe and zero for
    // the other seven. The widget's paint event is not involved in anything here:
    // every draw happens in a pass this file owns, inside main().
    gl::Texture colorTex;
    colorTex.create(QSize(kWidth, kHeight)); // RGBA8: gl::Texture's default format
    gl::Framebuffer fbo;
    fbo.setColorAttachment(0, colorTex.id());
    CHECK_MSG(
        fbo.isComplete(),
        "the harness FBO must be complete: without it every count would come from whatever framebuffer"
        " happened to be bound"
    );

    // The text probes. The engine caches a glyph texture by its text, so both
    // probes use the same string; the font has to have real metrics or the engine
    // would ask for a 0x0 texture (gl::Texture::create asserts on one).
    QFont labelFont = QApplication::font();
    labelFont.setPixelSize(28);
    const QString labelText = QStringLiteral("88");
    CHECK_MSG(
        !QFontMetrics(labelFont).boundingRect(labelText).isEmpty(),
        "no font in this environment lays this text out: the text probes cannot be measured without glyph"
        " metrics"
    );

    // A small solid pixmap for the blit probe. The engine's drawPixmap only takes
    // a raster-backed QPixmap - it drops a GL-backed one on the floor - so that is
    // asserted rather than hoped for.
    QPixmap patch(64, 48);
    patch.fill(kInk);
    CHECK_MSG(
        patch.paintEngine() && patch.paintEngine()->type() == QPaintEngine::Raster,
        "the engine's drawPixmap silently ignores a non-raster QPixmap, so this probe would measure nothing"
    );

    const std::vector<Probe> probes = {
        // 0: the machinery's own control. Nothing is drawn; nothing may count.
        {"control",
         "nothing is drawn on this pass: a non-zero count would mean the readback or the counting invents"
         " pixels, and every other number here would be worthless",
         kWholeImage,
         false,
         [](QPainter&, const QPoint&) {}},

        // 1: a pen stroke. This is the call the graph panel's grid and curve
        //    strokes use.
        {"line",
         "a solid pen stroke must paint: QPainter::drawLine goes to the engine's pen path"
         " (drawOutline -> PrimitiveDrawer::drawLine)",
         0,
         true,
         [](QPainter& p, const QPoint& o) {
             p.setPen(QPen(kInk, 3.0));
             p.drawLine(QPointF(o.x() + 12, o.y() + 24), QPointF(o.x() + 88, o.y() + 120));
         }},

        // 2: a filled rect, i.e. the engine's brush path.
        {"rect",
         "a filled rect must paint: with no pen scheduled the fill alone is what the engine's drawRect"
         " schedules",
         1,
         true,
         [](QPainter& p, const QPoint& o) {
             p.setPen(Qt::NoPen);
             p.setBrush(QBrush(kInk));
             p.drawRect(QRectF(o.x() + 20, o.y() + 35, 60, 80));
         }},

        // 3: a stroked polyline, which Qt routes to drawPolygon(PolylineMode).
        {"polyline",
         "a stroked polyline must paint: QPainter routes it to drawPolygon(PolylineMode) and the engine"
         " strokes it segment by segment",
         2,
         true,
         [](QPainter& p, const QPoint& o) {
             p.setPen(QPen(kInk, 3.0));
             p.setBrush(Qt::NoBrush);
             QPolygonF poly;
             poly << QPointF(o.x() + 8, o.y() + 120) << QPointF(o.x() + 35, o.y() + 30)
                  << QPointF(o.x() + 65, o.y() + 110) << QPointF(o.x() + 92, o.y() + 20);
             p.drawPolyline(poly);
         }},

        // 4: a filled ellipse - the shape the panel's key dots are drawn with.
        {"ellipse",
         "a filled ellipse must paint: the graph panel's key dots are drawn with drawEllipse",
         3,
         true,
         [](QPainter& p, const QPoint& o) {
             p.setPen(QPen(kInk, 1.0));
             p.setBrush(QBrush(kInk));
             p.drawEllipse(QRectF(o.x() + 22, o.y() + 40, 56, 70));
         }},

        // 5: a filled convex polygon.
        {"convex_polygon",
         "a filled convex polygon must paint: QPainter routes it to drawPolygon(ConvexMode) and the engine"
         " fills it",
         4,
         true,
         [](QPainter& p, const QPoint& o) {
             p.setPen(Qt::NoPen);
             p.setBrush(QBrush(kInk));
             QPolygonF tri;
             tri << QPointF(o.x() + 50, o.y() + 30) << QPointF(o.x() + 88, o.y() + 118)
                 << QPointF(o.x() + 12, o.y() + 118);
             p.drawConvexPolygon(tri);
         }},

        // 6: a texture blit. A TEXTURE draw is queued with usePen = false, so what
        //    gates it is the scheduled brush - and the brush colour is also the
        //    texture's tint.
        {"pixmap",
         "a solid pixmap blit must paint: drawPixmap uploads the pixmap as a texture, and its tint is the"
         " scheduled brush, so this probe also needs the brush it sets",
         5,
         true,
         [patch](QPainter& p, const QPoint& o) {
             p.setPen(Qt::NoPen);
             p.setBrush(QBrush(kInk));
             p.drawPixmap(QRectF(o.x() + 18, o.y() + 35, 64, 48), patch, QRectF(patch.rect()));
         }},

        // 7: rule 1 - a path paints nothing. drawRoundedRect is the call a caller
        //    reaches for first, and QPainter routes it through drawPath.
        {"path",
         "drawRoundedRect (a QPainterPath) paints 0 pixels: GLCorePaintEngine::drawPath is a silent no-op"
         " (src/ctrl/Painter.cpp:127), so paths, fillPath and drawRoundedRect paint nothing in a"
         " USE_GL_CORE_PROFILE build - if path support is ever added to that engine, this harness must be"
         " updated rather than merely kept passing",
         6,
         false,
         [](QPainter& p, const QPoint& o) {
             p.setPen(QPen(kInk, 2.0));
             p.setBrush(QBrush(kInk));
             p.drawRoundedRect(QRectF(o.x() + 20, o.y() + 40, 60, 70), 10.0, 10.0);
         }},

        // 8/9: rule 2, as a pair. Same text, same font, same position, same pen,
        //      same cell - the ONLY difference is the scheduled brush. Each probe
        //      sets the brush twice, in opposite order, because a painter whose
        //      brush already equals the value being set need not report
        //      DirtyBrush at all; the pair must not depend on that.
        {"text_with_brush",
         "the same text as text_without_brush, in the same cell, with a brush scheduled: a brush must make"
         " the engine's glyph blit paint",
         7,
         true,
         [labelFont, labelText](QPainter& p, const QPoint& o) {
             p.setFont(labelFont);
             p.setPen(QPen(kInk));
             p.setBrush(Qt::NoBrush);
             p.setBrush(QBrush(kInk));
             p.drawText(QPointF(o.x() + 24, o.y() + 90), labelText);
         }},
        {"text_without_brush",
         "drawText with NO brush scheduled paints 0 pixels: drawTextItem blits a glyph texture, a"
         " usePen = false draw, and gl::PrimitiveDrawer::pushDrawCommand returns early for those unless a"
         " brush is scheduled - this is the rule that silently ate the graph panel's axis labels; if that"
         " early-out is ever relaxed, this harness must be updated",
         7,
         false,
         [labelFont, labelText](QPainter& p, const QPoint& o) {
             p.setFont(labelFont);
             p.setPen(QPen(kInk));
             p.setBrush(QBrush(kInk));
             p.setBrush(Qt::NoBrush);
             p.drawText(QPointF(o.x() + 24, o.y() + 90), labelText);
         }},

        // 10: the engine's batched GL_POINTS path (the weight heatmap is a GPU
        //     triangle surface now, but this remains the point primitive's contract).
        //     The pen width is the point size: one 10px point must paint far more
        //     than a 1px point would, which is what minPixels pins.
        {"points",
         "one 10px point must paint a disc, not a 1px speck: the heatmap cannot carry its field without the"
         " engine applying the pen width as glPointSize",
         0,
         true,
         [](QPainter& p, const QPoint& o) {
             p.setPen(QPen(kInk, 10.0));
             p.setBrush(Qt::NoBrush);
             const QPointF point(o.x() + 50, o.y() + 75);
             p.drawPoints(&point, 1);
         },
         50},

        // 11: the batch split. The overlay's drawer holds 8192 vertices per slot,
        //     so drawPoints() must split a cloud across slots instead of dropping
        //     what does not fit. The cloud covers the cell; if only the first
        //     chunk were drawn the second half of the cell would stay clear.
        {"points_batch",
         "a 9000-point cloud (past the drawer's 8192-vertex slot) must paint across the whole cell: a"
         " dropped second chunk leaves the lower half clear",
         2,
         true,
         [](QPainter& p, const QPoint& o) {
             p.setPen(QPen(kInk, 2.0));
             p.setBrush(Qt::NoBrush);
             std::vector<QPointF> cloud;
             cloud.reserve(9000);
             for (int y = 0; y < 90; ++y) {
                 for (int x = 0; x < 100; ++x) {
                     cloud.emplace_back(o.x() + 1 + x, o.y() + 1 + y * 1.6);
                 }
             }
             p.drawPoints(cloud.data(), int(cloud.size()));
         },
         8000},

        // 12: the authored-weight field is a GPU triangle surface, not an image:
        //     one indexed quad with per-vertex weights must paint its whole area.
        //     A regression to "draw the vertices" would leave the middle clear, and
        //     a missing draw leaves the whole cell magenta.
        {"weight_field_surface",
         "a 60x60 weight quad (weights 0.25..0.75) must paint its whole area: a point cloud would paint"
         " the four corners only, and an absent draw paints nothing",
         5,
         true,
         [](QPainter& p, const QPoint& o) { drawWeightFieldQuad(p, o, 0.25f, 0.75f); },
         3000},

        // 13: the same quad with every weight below the field threshold must paint
        //     NOTHING. The fragment shader's discard is the whole difference between
        //     "a field over the bone's area" and "a blue wash over the layer".
        {"weight_field_below_threshold",
         "a weight quad whose weights are all below the field threshold must paint 0 pixels: the"
         " fragment shader discards them instead of washing the layer blue",
         6,
         false,
         [](QPainter& p, const QPoint& o) { drawWeightFieldQuad(p, o, 0.0f, 0.001f); }},
    };

    for (const Probe& probe : probes) {
        const QRect cell = (probe.cell == kWholeImage) ? QRect(0, 0, kWidth, kHeight) : cellRect(probe.cell);

        fbo.bind();
        funcs->glViewport(0, 0, kWidth, kHeight);
        funcs->glClearColor(kClear.redF(), kClear.greenF(), kClear.blueF(), kClear.alphaF());
        funcs->glClear(GL_COLOR_BUFFER_BIT);

        {
            // One handle per probe: nothing of one pass survives into the next.
            ctrl::PainterHandle handle;
            QPainter* p = handle.begin(glWidget);
            CHECK_MSG(
                p->isActive(),
                "the overlay engine refused this device: a no-op QPainter paints nothing, and every count"
                " below would be a measurement of nothing"
            );
            p->setRenderHint(QPainter::Antialiasing);
            probe.draw(*p, cell.topLeft());
            handle.end();
        }

        const QImage frame = readFboTopDown();
        fbo.release();

        const int pixels = paintedCount(frame, cell);
        std::printf("MEASURED %s pixels=%d\n", probe.name, pixels);

        char message[1024];
        const int minPixels = probe.minPixels > 0 ? probe.minPixels : 1;
        std::snprintf(
            message,
            sizeof(message),
            "%s: expected %s but counted %d pixels%s - %s",
            probe.name,
            probe.expectPainted ? (probe.minPixels > 0 ? "at least the pixel count"
                                                       : "> 0")
                                : "0",
            pixels,
            probe.expectPainted
                ? " (a 0 here is either the engine dropping the draw or the readback geometry moving: the GL"
                  " readback is flipped vertically and each probe is counted only inside its own cell)"
                : " (a change here means this harness no longer pins what it claims)",
            probe.note
        );
        CHECK_MSG(probe.expectPainted ? (pixels >= minPixels) : (pixels == 0), message);
    }

    // ---- the weight field's INK: ramp, per-fragment interpolation, orientation ----
    // A separate pass, counted not by "did anything arrive" but by reading three pixels.
    // The quad's weight rises left to right (0.2 at x=120 to 0.8 at x=180), so the left
    // sample must be the cool end (blue), the middle the green stop and the right the hot
    // end (red). A mirrored model-view, a swapped weight attribute, a ramp evaluated once
    // per vertex or a flat vertex colour changes one of them.
    {
        fbo.bind();
        funcs->glViewport(0, 0, kWidth, kHeight);
        funcs->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        funcs->glClear(GL_COLOR_BUFFER_BIT);

        {
            ctrl::PainterHandle handle;
            QPainter* p = handle.begin(glWidget);
            CHECK_MSG(p->isActive(), "the weight-field ink pass got no painter");
            drawWeightFieldQuad(*p, QPoint(100, 100), 0.2f, 0.8f);
            handle.end();
        }

        const QImage frame = readFboTopDown();
        fbo.release();

        // The quad spans x 120..180, y 145..205. The three samples are 30/50/70 px in.
        const uchar* left = pixelAt(frame, 130, 175);
        const uchar* mid = pixelAt(frame, 150, 175);
        const uchar* right = pixelAt(frame, 170, 175);
        std::printf(
            "MEASURED weight_field_ink left=%d,%d,%d,%d middle=%d,%d,%d,%d right=%d,%d,%d,%d\n",
            left[0], left[1], left[2], left[3], mid[0], mid[1], mid[2], mid[3],
            right[0], right[1], right[2], right[3]
        );
        CHECK_MSG(
            right[0] > left[0] + 50,
            "the ramp must run cool to hot left-to-right: the hot end is not redder than the cool one"
            " (a mirrored model-view or a swapped weight attribute puts 0.8 on the left)"
        );
        CHECK_MSG(
            left[2] > right[2] + 50,
            "the cool end must keep more blue than the hot end (the ramp is not applied, or the"
            " interpolation is not per fragment)"
        );
        CHECK_MSG(
            mid[1] > mid[0] + 50,
            "the field's middle must reach the ramp's green stop: a flat vertex colour or a per-vertex"
            " ramp would not"
        );
        CHECK_MSG(
            left[3] == 255 && mid[3] == 255 && right[3] == 255,
            "the wash must keep the canvas OPAQUE: a hole here composites the window behind the viewport"
        );
    }

    std::printf("OK: overlay engine primitive subset pinned (%d probes)\n", int(probes.size()));
    return 0;
}
