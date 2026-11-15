#ifndef GUI_INK_H
#define GUI_INK_H

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QVector>

namespace gui {
namespace ink {

// THE PANEL'S DRAWING VOCABULARY, and the reason the graph editor can be re-hosted at all.
//
// The graph panel's ink used to be expressed AS ImGui calls: `AddRectFilled`, `AddLine`,
// `AddCircleFilled`, `PushClipRect`, `AddText` - inside a single 800-line function that also
// makes every SHAPE decision (which rect, which radius, which colour, which clip, which
// alignment). Swapping the rasteriser therefore meant either rewriting that function (two
// copies to keep in step, for ever) or keeping ImGui forever.
//
// This interface splits the two halves. The caller keeps every decision and calls ONE
// vocabulary; a BACKEND decides how a fill, a stroke, a circle, a run of text or a clip is
// rasterised. There is exactly one implementation of the decisions, so three backends cannot
// disagree about geometry, colour, order or clipping - they can only disagree about
// RASTERISATION, which is measurable and is what the parity gate measures.
//
// The vocabulary is deliberately the smallest set the panel actually uses. It is NOT a
// general 2D API: adding a primitive is a decision that every backend must answer.
//
// COLOURS ARE QColor, never a packed integer. `ImU32` and `QRgb` are both `unsigned int` and
// pack in OPPOSITE channel orders (ImGui A<<24|B<<16|G<<8|R, QRgb A<<24|R<<16|G<<8|B), so a
// packed value handed to a QColor compiles and swaps red with blue - a defect this tree has
// already paid for once (the ghost handle stem). A backend converts at its own boundary.

// Horizontal alignment of a run inside the box the caller measured.
enum class HAlign { Left, Centre, Right };

// Where a run's GLYPH LINE BOX sits inside the box's rows. Two axes, because the horizontal
// one is the label's job (a value number is right-aligned in its column; a frame number's box
// is centred on its tick and the run is anchored LEFT in it) while the vertical one is whose
// box the model published - the ruler's numbers start their INK on the row the timeline's own
// numbers ink, which is not the same as centring a glyph in the box.
enum class VAlign { Centre, Top };

// The pointer, as the ink needs it: hover answers (a marker, a handle, a chip, a bar) and the
// press that is still down (a bar stays hot while its thumb is dragged, even after the pointer
// leaves the lane). A backend never reads the pointer itself: the HOST supplies it, so a
// headless gate can state any pointer it wants and a Qt host passes its own.
struct Pointer {
    QPointF pos{-1.0, -1.0};
    QPointF pressPos{-1.0, -1.0};
    bool leftDown = false;
};

// One drawing surface. Every call is in LOGICAL pixels in the host's own coordinate space.
//
// RECT CONVENTION: a `QRectF(x, y, w, h)` names the SAME pixels as `QRect(x, y, w, h)`. That is
// the one statement every backend needs, because the two Qt rect types disagree about `right()`
// (QRect's is x+w-1, QRectF's is x+w) - so a backend computes its far edge from the rect it was
// given and the conversion stays a value copy, never an adjustment.
class Ink {
public:
    virtual ~Ink() = default;

    // The antialias state of LINES. The panel's chrome (ticks, the hairline under the ruler,
    // the zero rule, the playhead) asks for AA OFF explicitly: an AA'd 1px line straddles two
    // columns and reads as 2px of grey, and every one of those marks is asked for an INTEGER
    // column or row. Filled shapes are always antialiased.
    virtual void setLineAntialias(bool aEnabled) = 0;

    // The clip stack. Rects are inclusive like every other rect here; a backend applies the
    // intersection of the whole stack, and `popClip` restores the previous one.
    virtual void pushClip(const QRect& aRect) = 0;
    virtual void popClip() = 0;

    // A filled rectangle, optionally with rounded corners (radius in logical px).
    virtual void fillRect(const QRectF& aRect, const QColor& aColor, double aRadius = 0.0) = 0;

    // A stroked rectangle: the marquee's edge. `aWidth` is the stroke's full width.
    virtual void strokeRect(const QRectF& aRect, const QColor& aColor, double aRadius, double aWidth) = 0;

    // A straight line. This is the CHROME primitive: the caller has already decided the line
    // is one pixel or more and whether it is antialiased, and the endpoints are the positions
    // the model asked for (a backend owns the half-pixel conventions of its own rasteriser,
    // because they differ: ImGui's non-AA 1px path is half-open, Canvas2D centres the stroke).
    virtual void line(const QPointF& aFrom, const QPointF& aTo, const QColor& aColor, double aWidth) = 0;

    // A path stroke through the points, with round joins and caps: the curve ink. `halfWidth`
    // is half the stroke's full width, in logical px. The panel's is a per-fragment coverage
    // pass (gui::stroke in GraphPanelFrame.cpp) where the backend can run it; a backend that
    // cannot MUST stroke the same polyline with the same width, never approximate the path.
    virtual void stroke(const QVector<QPointF>& aPoints, const QColor& aColor, double aHalfWidth) = 0;

    // A filled circle, and a circle's 1px-class edge stroke. `aRadius` is the circle the ink
    // must occupy: a backend strokes CENTRED on it (the caller adds the half pixel its own
    // rasteriser needs, if any, by asking for a radius that accounts for it).
    virtual void circleFill(const QPointF& aCentre, double aRadius, const QColor& aColor) = 0;
    virtual void circleStroke(const QPointF& aCentre, double aRadius, const QColor& aColor, double aWidth) = 0;

    // One run of text inside the box the caller measured. The run's ORIGIN is snapped to a
    // whole pixel (the caller's alignment arithmetic lands on fractions, and a fractional
    // origin is what makes a glyph's cell and its ink disagree by a column), and VAlign::Top
    // aligns the run's INK to the same row the timeline's own labels ink (a backend subtracts
    // its font's own top bearing to get there, so the row holds at any size).
    //
    // `aRightGap` is taken from the box's own SPARE width, so a run that fills its box is left
    // exactly where right-alignment puts it and the gap can never push it out of the column.
    // `aHalo` is the ring a label may be drawn over (the panel's own theme tone); an INVALID
    // colour means "no halo", which is the shipped state (gui::kDrawTextHalo is false and the
    // panel's contrast-outline pass does the ringing). A backend that cannot ring text must
    // still draw the run.
    virtual void text(
        const QRect& aBox, const QString& aText, HAlign aAlign, VAlign aVAlign, const QColor& aColor,
        const QColor& aHalo = QColor(), int aRightGap = 0
    ) = 0;
};

// THE BOX-SELECT MARQUEE, ONE DECISION FOR BOTH VIEWS.
//
// `aRect` is the gesture's RAW rect (graph: GraphEditor::marqueeRect; timeline:
// SelectionBox::rect) - and it is passed to the backend unchanged, because THE HALF PIXEL
// IS THE RASTERISER'S CONVENTION, not a design decision: a centred 1px stroke on an
// integer edge straddles two device rows, so a backend whose stroke is centred must
// inset by half its width itself, while ImGui's antialiased 1px line already lands on the
// row its coordinate names. Measured on this gate: with the half pixel applied HERE the
// ImGui host split the edge over two rows (`#d543fd` / `#d346fd`, 55 away from the edge
// token) instead of painting it; with the raw rect it lands on one row, distance 1.
inline void paintMarquee(
    Ink& aInk, const QRectF& aRect, const QColor& aFill, const QColor& aEdge, double aRadius
) {
    aInk.fillRect(aRect, aFill, aRadius);
    aInk.strokeRect(aRect, aEdge, aRadius, 1.0);
}

} // namespace ink
} // namespace gui

#endif // GUI_INK_H
