#ifndef CTRL_TIME_TIMEBODYMODEL_H
#define CTRL_TIME_TIMEBODYMODEL_H

#include <QColor>
#include <QRect>
#include <QString>
#include <QVector>

#include "ctrl/TimeLineRow.h"
#include "ctrl/time/time_Renderer.h"
#include "ctrl/time/time_Scaler.h"
#include "util/Range.h"

class QPainter;

namespace ctrl {
namespace time {

// THE TIMELINE BODY'S FIRST MODEL: the lane pass - each lane's fill, its type
// separators and its lane labels, then the seam hairlines across the whole strip.
//
// WHY A MODEL. The body is migrating to Canvas Painter (see ctrl/time/time_Renderer.h for the
// ruler, which already paints both ways). A paint routine that reads the document and
// draws in one pass cannot be re-hosted; a model that states WHAT to draw can. This is
// the smallest honest slice of that: the pass that decides every lane's geometry and
// ink, with the keys, the range markers and the marquee still drawn by the caller (they
// are the next slices).
//
// ENGINE-NEUTRAL: geometry and colour only, no QPainter. Lane order is the paint order,
// because the shipped pass interleaves a lane's fill, its separators and its labels row
// by row - a later lane's fill must not paint over an earlier lane's ink.

struct Separator {
    int x0 = 0;
    int x1 = 0;
    double y = 0.0; // fractional: the lane is split into equal parts, not whole rows
};

struct Label {
    QRect box;
    QString text;
    QColor ink; // the channel's hue at the timeline's label alpha
};

struct Lane {
    QRect rect;
    bool selected = false;
    // A folder's summary band: a strip under the folder's own lanes carrying one
    // rounded triangle per frame that has a hidden descendant key. It uses the
    // lane's own surface - the separation and the marker shape are the signal,
    // not a second colour.
    bool summary = false;
    QVector<Separator> separators;
    QVector<Label> labels;
};

struct Seam {
    int x0 = 0;
    int x1 = 0;
    int y = 0;
};

// A KEY MARKER: the shape a key draws, its ink, and the selection ring outside it.
enum class KeyShape {
    Ellipse, // the ordinary round key
    Polygon  // a slimmed lane's diamond, a folder's holder diamond, a closed folder's triangle
};

struct KeyMark {
    KeyShape shape = KeyShape::Ellipse;
    QPointF centre;
    double radiusX = 0.0;
    double radiusY = 0.0;
    QPolygonF polygon;
    QColor fill;
    // A PROPORTIONAL key is a RING: `hollow` says so, and the geometry is published
    // here so the QPainter and Canvas hosts fill the same two shapes without either
    // re-deriving them. The hue disc ends `radius - kProportionalRingOuterInset`; the
    // hole (filled with holeInk, the LANE's own ink) is `hollowRadius` for an ellipse
    // and hollowHolePolygon for a polygon; hollowPolygon is the hue polygon's inset
    // copy. The outer `polygon`/radiusX/Y and the edge are the ordinary key's.
    bool hollow = false;
    double hollowRadius = 0.0;
    QColor holeInk;
    QPolygonF hollowPolygon;
    QPolygonF hollowHolePolygon;
    bool hasRing = false;
    double ringRadius = 0.0;
    QColor ring;
};

// One key pass. `edge` is the marker's outline pen (theme::Colors::outline for a lane's keys,
// baseKey for a closed folder's summary); `child` says which.
struct KeyPass {
    QVector<KeyMark> marks;
    QColor edge;
    bool child = false;
    // Summary triangles are FILL-ONLY: a 1px edge on a 5px neutral marker reads
    // as a dirty rim, not as definition.
    bool stroke = true;
};

// THE FRAME RANGE'S GUIDES: two vertical hairlines at the project's first and last frame,
// continuing the ruler's boundary ticks down through the lanes.
struct RangeMarker {
    int x = 0;
    int top = 0;
    int bottom = 0;
    QColor ink;
};

// THE MARQUEE: the drag selection's box - a rounded SOLID edge in the accent ink over a
// light accent wash, the same treatment as the graph panel's box select.
struct SelectionBox {
    bool visible = false;
    QRectF rect;
    QColor edge;
    QColor fill;
    double radius = 6.0;
};

// The OPAQUE result of painting aTop over aBottom, for a key's hole: the lane inks are
// translucent (body is base; the selection tint is 6% accent), so filling a hole with the
// raw lane ink would let the hue disc under it show through at 30%. The lane band is the
// composite, and the hole must be the composite too.
QColor opaqueOver(const QColor& aTop, const QColor& aBottom);

// The four lane inks, read from theme::TimeLine once so the painter takes no palette.
struct Inks {
    QColor body;
    QColor bodySelected;
    QColor edge;
    QColor separator;
};

struct Lines {
    QVector<Lane> lanes;
    QVector<Seam> seams;
    Inks inks;
};

// THE WHOLE BODY as one value, in PAINT ORDER: the lanes, the frame-range guides, the key
// passes (a closed folder's summary before its own keys, row by row) and the marquee. A host
// paints these four in that order and gets the shipped pass - QPainter here, Canvas Painter in
// gui/TimeBodyCanvas.h - with no document access at all.
struct BodyScene {
    Lines lines;
    QVector<RangeMarker> markers;
    QVector<KeyPass> keyPasses;
    SelectionBox selection;
};

// Reads the rows the timeline view pushed (`ctrl::TimeLineRow`) and states the pass.
// Vertical-band culling is the caller's rule and is applied here, so the model is already
// what the viewport can see.
Lines buildLines(
    const QVector<ctrl::TimeLineRow>& aRows, const QRect& aCameraRect, const QRect& aCullRect, const Inks& aInks
);

// The shipped QPainter rendering of that model - byte-for-byte the pass the timeline
// drew before the extraction (the equivalence harness compares it with the original).
void paintLines(QPainter& aPainter, const Lines& aLines);

// THE KEYS: a lane's own markers (shaped by the lane's slimmed state and the key's ability to
// hold children) and, for a closed folder, one rounded summary triangle per frame in its
// dedicated summary band.
KeyPass buildKeys(
    const core::ObjectNode* aNode, const TimeLineRow& aRow, const ctrl::time::Scaler& aScale,
    const util::Range& aRange, int aMargin, const QColor& aLaneInk
);
KeyPass buildChildKeys(
    const core::ObjectNode* aNode, const QRect& aSummaryRect, int aMargin,
    const ctrl::time::Scaler& aScale, const util::Range& aRange
);
void paintKeys(QPainter& aPainter, const KeyPass& aPass);

// The frame range's two guides (empty for a degenerate range, where both would collapse onto
// one column) and the marquee from the rect the gesture holds.
QVector<RangeMarker> buildRangeMarkers(
    const QRect& aCameraRect, const util::Range& aProjectRange, bool aPlayWindowActive,
    const ctrl::time::Scaler& aScale, int aMargin
);
SelectionBox buildMarquee(const QRect& aRect);
void paintGuides(QPainter& aPainter, const QVector<RangeMarker>& aMarkers, const SelectionBox& aMarquee);

// Reads the rows and the view state and states every pass. `aCullRect` is already widened by
// the caller (the widget's damage rect + 2px, as the shipped render used).
BodyScene buildBodyScene(
    const QVector<TimeLineRow>& aRows, const QRect& aCameraRect, const QRect& aCullRect,
    const ctrl::time::Scaler& aScale, const util::Range& aRange, const util::Range& aProjectRange,
    bool aPlayWindowActive, const QRect& aMarqueeRect, int aMargin, const Inks& aInks
);

// Paints a scene in its recorded order (the one QPainter host).
void paintBodyScene(QPainter& aPainter, const BodyScene& aScene);

} // namespace time
} // namespace ctrl

#endif // CTRL_TIME_TIMEBODYMODEL_H
