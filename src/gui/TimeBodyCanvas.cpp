#include "gui/TimeBodyCanvas.h"

#include "gui/InkCanvas.h"

#include <QCanvasPainter>
#include <QFont>
#include <QRectF>

namespace gui {
namespace timeBodyCanvas {

namespace {

constexpr float kHalf = 0.5f;
constexpr int kTypeDash = 4;
constexpr int kTypeGap = 4;

void strokePath(QCanvasPainter& aPainter, const QPolygonF& aPolygon) {
    if (aPolygon.isEmpty())
        return;
    aPainter.beginPath();
    aPainter.moveTo(aPolygon.first());
    for (int i = 1; i < aPolygon.size(); ++i)
        aPainter.lineTo(aPolygon.at(i));
    aPainter.closePath();
}

} // namespace

void paintLinesCanvas(QCanvasPainter& aPainter, const ctrl::time::Lines& aLines) {
    aPainter.setAntialias(0.0f);
    for (const ctrl::time::Lane& lane : aLines.lanes) {
        aPainter.setFillStyle(lane.selected ? aLines.inks.bodySelected : aLines.inks.body);
        aPainter.fillRect(QRectF(lane.rect));
        if (!lane.separators.isEmpty()) {
            // TYPE separators: the same 4-on/4-off dash as QPainter, drawn as 1px
            // marks because Canvas2D has no dashed stroke. The solid 70% seams
            // between rows/layers are a different concept and stay solid.
            aPainter.setFillStyle(aLines.inks.separator);
            for (const ctrl::time::Separator& each : lane.separators) {
                const int y = int(each.y);
                for (int x = each.x0; x < each.x1; x += kTypeDash + kTypeGap) {
                    const int end = std::min(x + kTypeDash, each.x1);
                    aPainter.fillRect(QRectF(x, y, end - x, 1));
                }
            }
        }
        // NOT inside the separator branch: a lane with ONE valid type has no separator, and
        // MEASURED 2026-10-22 that is exactly the lane whose label vanished - the timeline's
        // first lane lost its "Move", on every project whose lanes hold a single type each.
        // The gate could not see it: a pixel inside a label's box counts as "expected" (two
        // text engines cannot agree pixel for pixel), so a MISSING label was expected too.
        if (!lane.labels.isEmpty()) {
            aPainter.setFont(QFont());
            aPainter.setTextAlign(QCanvasPainter::TextAlign::Center);
            aPainter.setTextBaseline(QCanvasPainter::TextBaseline::Middle);
            for (const ctrl::time::Label& label : lane.labels) {
                aPainter.setFillStyle(label.ink);
                aPainter.fillText(
                    label.text, float(label.box.center().x()) + kHalf, float(label.box.center().y()) + kHalf
                );
            }
        }
    }

    aPainter.setAntialias(0.0f);
    // The seams: 1px rows. A FILLED rect is exact where a stroke is not: a 1px stroke at
    // y+0.5 with the antialias fringe inks the row above and below as well (measured: three
    // rows for one line).
    aPainter.setFillStyle(aLines.inks.edge);
    for (const ctrl::time::Seam& seam : aLines.seams)
        // QPainter's drawLine(x0, y, x1, y) with AA off inks the columns whose CENTRES lie on
        // the segment: x0 .. x1-1, i.e. a width of (x1 - x0).
        aPainter.fillRect(QRectF(seam.x0, seam.y, seam.x1 - seam.x0, 1));
}

void paintKeysCanvas(QCanvasPainter& aPainter, const ctrl::time::KeyPass& aPass) {
    aPainter.setAntialias(1.0f);
    for (const ctrl::time::KeyMark& mark : aPass.marks) {
        // A path is NOT reset by fill()/stroke(): without beginPath() every marker's shape
        // would be appended to the previous one's and re-inked with the next style.
        if (mark.hollow) {
            // A proportional key is a RING, built from two fills and the ordinary edge
            // (the QPainter pass draws the same geometry). Fills, not strokes: a stroked
            // ring of this size put the two engines' antialiasing 69 pixels apart on the
            // parity gate.
            aPainter.setFillStyle(mark.fill);
            aPainter.beginPath();
            if (mark.shape == ctrl::time::KeyShape::Ellipse) {
                aPainter.circle(
                    float(mark.centre.x()), float(mark.centre.y()),
                    float(mark.radiusX - ctrl::time::kProportionalRingOuterInset)
                );
            } else {
                strokePath(aPainter, mark.hollowPolygon);
            }
            aPainter.fill();
            aPainter.setFillStyle(mark.holeInk);
            aPainter.beginPath();
            if (mark.shape == ctrl::time::KeyShape::Ellipse) {
                aPainter.circle(float(mark.centre.x()), float(mark.centre.y()), float(mark.hollowRadius));
            } else {
                strokePath(aPainter, mark.hollowHolePolygon);
            }
            aPainter.fill();
            aPainter.setStrokeStyle(aPass.edge);
            aPainter.setLineWidth(1.0f);
            aPainter.beginPath();
            if (mark.shape == ctrl::time::KeyShape::Ellipse) {
                aPainter.circle(float(mark.centre.x()), float(mark.centre.y()), float(mark.radiusX));
            } else {
                strokePath(aPainter, mark.polygon);
            }
            aPainter.stroke();
        } else {
            aPainter.setFillStyle(mark.fill);
            if (aPass.stroke) {
                aPainter.setStrokeStyle(aPass.edge);
                aPainter.setLineWidth(1.0f);
            }
            aPainter.beginPath();
            if (mark.shape == ctrl::time::KeyShape::Ellipse) {
                aPainter.circle(float(mark.centre.x()), float(mark.centre.y()), float(mark.radiusX));
                aPainter.fill();
                if (aPass.stroke)
                    aPainter.stroke();
            } else {
                strokePath(aPainter, mark.polygon);
                aPainter.fill();
                if (aPass.stroke)
                    aPainter.stroke();
            }
        }
        if (mark.hasRing) {
            aPainter.setStrokeStyle(mark.ring);
            aPainter.setLineWidth(1.0f);
            aPainter.beginPath();
            aPainter.circle(float(mark.centre.x()), float(mark.centre.y()), float(mark.ringRadius));
            aPainter.stroke();
        }
    }
}

void paintGuidesCanvas(
    QCanvasPainter& aPainter, const QVector<ctrl::time::RangeMarker>& aMarkers,
    const ctrl::time::SelectionBox& aMarquee
) {
    aPainter.setAntialias(0.0f);
    for (const ctrl::time::RangeMarker& marker : aMarkers) {
        // A filled 1px column, for the same reason as the seams.
        aPainter.setFillStyle(marker.ink);
        // A 1px column needs its span to fall INSIDE one device column: [x+0.5, x+1.5) snaps to
        // column x+1, so a span of [x, x+1) is used with the fringe off.
        // MEASURED LIMIT: Canvas2D cannot fill a single DEVICE column. A span of [x, x+1) inks
        // three columns (x-1..x+1) and [x+0.5, x+1.5) two; `antialias: 0` does not change it
        // (it governs stroke fringes, not rect edges). The half-pixel span is the narrower of
        // the two, and the parity gate therefore counts the guide's columns as an expected
        // ink-path difference - like text and dashes - rather than as geometry.
        aPainter.fillRect(QRectF(float(marker.x) + 0.5f, float(marker.top), 1.0f, float(marker.bottom - marker.top + 1)));
    }
    if (!aMarquee.visible)
        return;
    // The SAME code the graph panel's box uses: gui::ink::paintMarquee over InkCanvas.
    // It owns the half-pixel inset that makes the 1px edge one column instead of two.
    gui::ink::InkCanvas ink(aPainter, QFont());
    gui::ink::paintMarquee(ink, aMarquee.rect, aMarquee.fill, aMarquee.edge, aMarquee.radius);
}

} // namespace timeBodyCanvas
} // namespace gui
