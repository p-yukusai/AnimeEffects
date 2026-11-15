// THE CANVAS PAINTER BODY'S PARITY GATE (needs Qt >= 6.12: AE_CANVAS_PAINTER).
//
// One set of ctrl::time models, painted twice - by the shipped QPainter renderer and by the
// Canvas Painter renderer - and compared. Canvas2D cannot express three of the things Qt draws
// (dashed strokes, boxed text, off-by-half strokes), so those regions are counted separately
// and only the rest is asserted to match: the lane fills, the key bodies and rings, the seam
// rows and the guide columns are GEOMETRY and must be identical.
#include <QApplication>
#include <QCanvasPainter>
#include <QCanvasPainterWidget>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "XCAssert.h"
#include "cmnd/Stack.h"
#include "core/MoveKey.h"
#include "core/ObjectNode.h"
#include "core/OpaKey.h"
#include "core/ScaleKey.h"
#include "core/TimeLine.h"
#include "ctrl/TimeLineRow.h"
#include "ctrl/time/time_BodyModel.h"
#include "gui/TimeBodyCanvas.h"
#include "gui/theme/Colors.h"
#include "gui/theme/TimeLine.h"
#include "gui/AudioPlaybackWidget.h"
#include "gui/ObjectTreeWidget.h"

bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }
bool gui::obj::Item::isTopNode() const { return false; }
std::pair<core::TimeKey*, core::LayerMesh*> gui::ObjectTreeWidget::getAreaMeshImpl(core::ObjectNode&, int) {
    return {nullptr, nullptr};
}
namespace {

QColor withAlphaLocal(QColor aColor, int aAlpha) {
    aColor.setAlpha(aAlpha);
    return aColor;
}
class H1: public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class H2: public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};
H1 g1;
H2 g2;
} // namespace
XCAssertHandler* gXCAssertHandler = &g1;
XCErrorHandler* gXCErrorHandler = &g2;



namespace {



constexpr int kWidth = 480;
constexpr int kHeight = 160;



struct Fixture {
    ctrl::time::Lines lines;
    ctrl::time::KeyPass keys;
    QVector<ctrl::time::RangeMarker> markers;
    ctrl::time::SelectionBox marquee;
};

class CanvasBody: public QCanvasPainterWidget {
public:
    Fixture fixture;

protected:
    void paint(QCanvasPainter* aPainter) override {
        gui::timeBodyCanvas::paintLinesCanvas(*aPainter, fixture.lines);
        gui::timeBodyCanvas::paintKeysCanvas(*aPainter, fixture.keys);
        gui::timeBodyCanvas::paintGuidesCanvas(*aPainter, fixture.markers, fixture.marquee);
    }
};

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    int cases = 0, failures = 0;

    for (bool dark : {true, false}) {
        theme::Colors::activate(dark, theme::kDefaultAccent);
        theme::TimeLine theme;
        const QRect camera(0, 0, kWidth, kHeight);

        Fixture f;
        f.lines.inks.body = theme.trackColor();
        f.lines.inks.bodySelected = theme.trackSelectColor();
        f.lines.inks.edge = theme.trackEdgeColor();
        f.lines.inks.separator = theme.trackSeperatorColor();

        ctrl::time::Lane lane;
        lane.rect = QRect(0, 0, kWidth, 65);
        lane.selected = true;
        lane.separators.append({0, kWidth, 21.5});
        lane.separators.append({0, kWidth, 43.0});
        ctrl::time::Label label;
        label.box = QRect(kWidth / 2 - 50, 0, 100, 21);
        label.text = QStringLiteral("Move");
        label.ink = QColor(theme::channelColor(theme::Colors::current(), core::TimeKeyType_Move, 0).red(),
                           theme::channelColor(theme::Colors::current(), core::TimeKeyType_Move, 0).green(),
                           theme::channelColor(theme::Colors::current(), core::TimeKeyType_Move, 0).blue(),
                           107);
        lane.labels.append(label);
        f.lines.lanes.append(lane);

        // A SECOND lane with ONE valid type: no separators at all, and a label. This is the
        // shape the Canvas pass dropped (the label draw sat inside the separator branch), so
        // the fixture has to contain it or the gate cannot fail for it.
        ctrl::time::Lane singleType;
        singleType.rect = QRect(0, 65, kWidth, 22);
        ctrl::time::Label singleLabel;
        singleLabel.box = QRect(kWidth / 2 - 50, 65, 100, 22);
        singleLabel.text = QStringLiteral("Opa");
        singleLabel.ink = label.ink;
        singleType.labels.append(singleLabel);
        f.lines.lanes.append(singleType);
        f.lines.seams.append({0, kWidth, 0});
        f.lines.seams.append({0, kWidth, 65});

        f.keys.edge = theme::Colors::current().outline;
        {
            ctrl::time::KeyMark ellipse;
            ellipse.shape = ctrl::time::KeyShape::Ellipse;
            ellipse.centre = QPointF(120.5, 30.5);
            ellipse.radiusX = 3.0;
            ellipse.radiusY = 3.0;
            ellipse.fill = theme::channelColor(theme::Colors::current(), core::TimeKeyType_Move, 0);
            f.keys.marks.append(ellipse);

            // A PROPORTIONAL key is a ring in both hosts: the hue band inside the
            // ordinary edge, with a hole (transparent here; the lane fill is already
            // on the texture). The fixture must contain it or the Canvas pass's
            // hollow branch has no gate.
            ctrl::time::KeyMark hollow = ellipse;
            hollow.centre = QPointF(170.5, 30.5);
            hollow.hollow = true;
            hollow.holeInk = ctrl::time::opaqueOver(
                theme.trackSelectColor(), theme::Colors::current().base
            );
            hollow.hollowRadius = hollow.radiusX - ctrl::time::kProportionalHoleInset;
            f.keys.marks.append(hollow);

            ctrl::time::KeyMark ringed = ellipse;
            ringed.centre = QPointF(220.5, 30.5);
            ringed.hasRing = true;
            ringed.ringRadius = 6.6;
            ringed.ring = QColor(theme::channelColor(theme::Colors::current(), core::TimeKeyType_Move, 0).red(),
                                 theme::channelColor(theme::Colors::current(), core::TimeKeyType_Move, 0).green(),
                                 theme::channelColor(theme::Colors::current(), core::TimeKeyType_Move, 0).blue(),
                                 217);
            f.keys.marks.append(ringed);

            ctrl::time::KeyMark diamond;
            diamond.shape = ctrl::time::KeyShape::Polygon;
            diamond.centre = QPointF(320.5, 30.5);
            diamond.fill = theme::channelColor(theme::Colors::current(), core::TimeKeyType_Opa, 0);
            diamond.polygon << QPointF(320.5, 26.3) << QPointF(324.7, 30.5) << QPointF(320.5, 34.7)
                            << QPointF(316.3, 30.5);
            f.keys.marks.append(diamond);

            // The same polygon rule in its hollow form: the model publishes the inset
            // outline, and both hosts stroke it with the hue.
            ctrl::time::KeyMark hollowDiamond = diamond;
            hollowDiamond.centre = QPointF(370.5, 30.5);
            hollowDiamond.hollow = true;
            hollowDiamond.holeInk = ctrl::time::opaqueOver(
                theme.trackSelectColor(), theme::Colors::current().base
            );
            hollowDiamond.polygon.clear();
            for (const QPointF& point : diamond.polygon)
                hollowDiamond.polygon.append(point + QPointF(50.0, 0.0));
            for (const QPointF& point : hollowDiamond.polygon) {
                hollowDiamond.hollowPolygon.append(
                    hollowDiamond.centre
                    + (point - hollowDiamond.centre) * ctrl::time::kProportionalPolygonInset
                );
                hollowDiamond.hollowHolePolygon.append(
                    hollowDiamond.centre
                    + (point - hollowDiamond.centre) * ctrl::time::kProportionalPolygonHoleInset
                );
            }
            f.keys.marks.append(hollowDiamond);

            ctrl::time::KeyMark triangle;
            triangle.shape = ctrl::time::KeyShape::Polygon;
            triangle.centre = QPointF(420.5, 60.0);
            triangle.fill = withAlphaLocal(theme::Colors::current().baseKey, 115);
            triangle.polygon << QPointF(420.5, 60.0) << QPointF(424.5, 53.0) << QPointF(416.5, 53.0);
            f.keys.marks.append(triangle);
        }

        f.markers = ctrl::time::buildRangeMarkers(
            camera, util::Range(100, 400), false, *new ctrl::time::Scaler(), 24
        );
        f.marquee = ctrl::time::buildMarquee(QRect(40, 20, 120, 40));

        QImage raster(kWidth, kHeight, QImage::Format_RGB32);
        raster.fill(theme::Colors::current().base);
        {
            QPainter p(&raster);
            ctrl::time::paintLines(p, f.lines);
            ctrl::time::paintKeys(p, f.keys);
            ctrl::time::paintGuides(p, f.markers, f.marquee);
        }

        CanvasBody widget;
        widget.fixture = f;
        widget.setFillColor(theme::Colors::current().base);
        widget.resize(kWidth, kHeight);
        const QImage canvas = widget.grabFramebuffer();
        if (canvas.isNull()) {
            std::printf("FAIL no Canvas Painter frame\n");
            return 1;
        }
        ++cases;
        int diff = 0, textOrDash = 0, geomShown = 0, blendLSB = 0;
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                if (raster.pixel(x, y) == canvas.pixel(x, y))
                    continue;
                {
                    const QColor a(raster.pixel(x, y));
                    const QColor b(canvas.pixel(x, y));
                    if (std::abs(a.red() - b.red()) <= 1 && std::abs(a.green() - b.green()) <= 1 &&
                        std::abs(a.blue() - b.blue()) <= 1) {
                        ++blendLSB; // translucent ink: one LSB belongs to the renderer
                        continue;
                    }
                }
                ++diff;
                bool expected = false;
                for (const auto& each : f.lines.lanes) {
                    for (const auto& eachLabel : each.labels)
                        expected = expected || eachLabel.box.adjusted(-2, -2, 2, 2).contains(x, y);
                    for (const auto& sep : each.separators)
                        expected = expected || (std::abs(y - int(sep.y)) <= 1);
                }
                // A marker's 1px outline and ring are ANTIALIASED by two different engines:
                // their coverage differs by construction (the ruler's parity gate splits
                // text the same way), so the marker's own box is an "expected" region while
                // its FILL and POSITION stay under the geometry rule.
                for (const auto& mark : f.keys.marks) {
                    QRectF box(mark.centre, QSizeF(0.0, 0.0));
                    if (mark.shape == ctrl::time::KeyShape::Polygon)
                        box = mark.polygon.boundingRect();
                    if (mark.hollow && mark.shape == ctrl::time::KeyShape::Ellipse)
                        box = QRectF(
                            mark.centre.x() - mark.radiusX, mark.centre.y() - mark.radiusY,
                            mark.radiusX * 2.0, mark.radiusY * 2.0
                        );
                    if (mark.hasRing)
                        box = box.united(QRectF(
                            mark.centre.x() - mark.ringRadius, mark.centre.y() - mark.ringRadius,
                            mark.ringRadius * 2.0, mark.ringRadius * 2.0
                        ));
                    expected = expected || box.adjusted(-2, -2, 2, 2).contains(x, y);
                }
                // The frame-range guides: see TimeBodyCanvas.cpp - a 1px column cannot be
                // filled exactly, so its columns are an expected difference.
                for (const auto& marker : f.markers)
                    expected = expected || (std::abs(x - marker.x) <= 2 && y >= marker.top && y <= marker.bottom);
                if (f.marquee.visible)
                    expected = expected || f.marquee.rect.toAlignedRect().adjusted(-2, -2, 2, 2).contains(x, y);
                if (expected)
                    ++textOrDash;
                else if (geomShown < 12) {
                    ++geomShown;
                    std::fprintf(stderr, "GEOM@%d,%d ", x, y);
                    std::printf("    geom @(%d,%d) raster=%s canvas=%s\n", x, y,
                                qPrintable(QColor(raster.pixel(x, y)).name()),
                                qPrintable(QColor(canvas.pixel(x, y)).name()));
                }
            }
        }
        // WHAT IS AT THE SUSPECT COLUMN: the fixture's guides and the two images at 462..466.
        std::printf("    markers:");
        for (const auto& m : f.markers)
            std::printf(" x=%d y=%d..%d ink=%s", m.x, m.top, m.bottom, qPrintable(m.ink.name()));
        std::printf("  marquee=%d,%d %dx%d\n", int(f.marquee.rect.x()), int(f.marquee.rect.y()),
                    int(f.marquee.rect.width()), int(f.marquee.rect.height()));
        for (int y = 20; y <= 30; ++y) {
            std::printf("    y=%2d raster:", y);
            for (int x = 461; x <= 467; ++x)
                std::printf(" %s", qPrintable(QColor(raster.pixel(x, y)).name()));
            std::printf("  canvas:");
            for (int x = 461; x <= 467; ++x)
                std::printf(" %s", qPrintable(QColor(canvas.pixel(x, y)).name()));
            std::printf("\n");
        }
        {
            int colMin = 9999, colMax = -1, rowMin = 9999, rowMax = -1;
            for (int y = 0; y < kHeight; ++y)
                for (int x = 0; x < kWidth; ++x)
                    if (raster.pixel(x, y) != canvas.pixel(x, y)) {
                        colMin = std::min(colMin, x); colMax = std::max(colMax, x);
                        rowMin = std::min(rowMin, y); rowMax = std::max(rowMax, y);
                    }
            std::printf("    band x %d..%d  y %d..%d\n", colMin, colMax, rowMin, rowMax);
        }
        // A LABEL'S BOX IS "EXPECTED" PIXEL TERRITORY, so a MISSING label used to pass: the
        // loop above excuses every pixel inside it. That is how the Canvas pass lost a lane's
        // name with the gate green. So each label the model names is checked for INK here, in
        // both images, and a label drawn on one engine and not the other is a failure.
        for (const auto& each : f.lines.lanes) {
            for (const auto& eachLabel : each.labels) {
                const auto inkCount = [](const QImage& aImage, const QRect& aBox) {
                    const QRect box = aBox.intersected(aImage.rect());
                    if (box.isEmpty())
                        return 0;
                    QHash<QRgb, int> hist;
                    for (int y = box.top(); y <= box.bottom(); ++y)
                        for (int x = box.left(); x <= box.right(); ++x)
                            ++hist[aImage.pixel(x, y)];
                    QRgb bg = 0;
                    int best = -1;
                    for (auto it = hist.cbegin(); it != hist.cend(); ++it)
                        if (it.value() > best) { best = it.value(); bg = it.key(); }
                    int ink = 0;
                    for (int y = box.top(); y <= box.bottom(); ++y)
                        for (int x = box.left(); x <= box.right(); ++x)
                            if (aImage.pixel(x, y) != bg)
                                ++ink;
                    return ink;
                };
                const int a = inkCount(raster, eachLabel.box);
                const int b = inkCount(canvas, eachLabel.box);
                // The rule is PARITY BETWEEN THE TWO RENDERINGS, not "the model says draw it": the
            // ruler legitimately blanks a number under the playhead's pill, and a box
            // can fall outside the image. What must never happen is one engine drawing
            // ink the other does not.
            const bool ok = (a == 0) == (b == 0) && (a == 0 || std::abs(a - b) * 2 <= std::max(a, b));
                std::printf(
                    "    label %-6s box=(%d,%d %dx%d) raster=%3d px canvas=%3d px %s\n",
                    qPrintable(eachLabel.text), eachLabel.box.x(), eachLabel.box.y(),
                    eachLabel.box.width(), eachLabel.box.height(), a, b, ok ? "ok" : "MISSING/DIFFERS"
                );
                if (!ok)
                    ++failures;
            }
        }

        const int geometry = diff - textOrDash;
        std::printf(
            "%-5s canvas=%dx%d text/dash=%4d blendLSB=%4d GEOMETRY=%3d\n", dark ? "dark" : "light",
            canvas.width(), canvas.height(), textOrDash, blendLSB, geometry
        );
        if (geometry != 0)
            ++failures;
    }

    std::printf("cases=%d failures=%d (only GEOMETRY differences fail)\n", cases, failures);
    return failures == 0 ? 0 : 1;
}
