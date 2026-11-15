#include "gui/FrameRulerCanvas.h"

#include <QCanvasPainter>
#include <QRectF>

namespace gui {
namespace frameRuler {

namespace {

// Canvas2D strokes are centred on the coordinates they are given, so a 1px mark on
// column x needs x + 0.5 to cover that column and nothing else. Every mark in the ruler
// is asked for an INTEGER column by the model, which is what makes this exact.
constexpr float kHalf = 0.5f;

} // namespace

void paintRulerCanvas(QCanvasPainter& aPainter, const Model& aModel, const PaintInk& aInk) {
    if (aModel.band.isEmpty())
        return;

    aPainter.save();
    aPainter.setAntialias(0.0f); // the ruler's chrome is integer-column ink, never feathered

    // TICKS: one length for every tier, prominence by alpha, and the shared 0.4 recession
    // outside the active playback range.
    // The raster renderer draws its tick from band.bottom() UP to bottom-kTickLength, and
    // an inclusive drawLine inks kTickLength+1 ROWS. In Canvas2D's edge coordinates a
    // 1px-wide stroke on column x is centred at x+0.5, and the segment must reach from the
    // last row's BOTTOM edge (bottom+1) to one row past the top one drawn.
    const float base = float(aModel.band.bottom()) + 1.0f;
    const float tickEnd = base - float(kTickLength + 1);
    for (const Tick& tick : aModel.ticks) {
        QColor colour = aInk.content;
        colour.setAlphaF(tickAlpha(tick.level) * (tick.inActiveRange ? 1.0 : kOutsideRangeFactor));
        aPainter.setStrokeStyle(colour);
        aPainter.setLineWidth(1.0f);
        aPainter.beginPath();
        aPainter.moveTo(float(tick.x) + kHalf, base);
        aPainter.lineTo(float(tick.x) + kHalf, tickEnd);
        aPainter.stroke();
    }

    // NUMBERS: anchored LEFT on the box the model published, top-aligned on the number
    // row - the convention both hosts state, and the one the graph panel's `LabelVAlign::
    // Top` exists for (the two rasterizers' glyph bearings differ, so the run is placed by
    // its ink row rather than centred).
    // The FONT is the host's (TimeRuler sets the widget's own before calling), so a themed
    // font size cannot apply to the raster ruler and not to this one.
    if (!aModel.labels.isEmpty()) {
        aPainter.setTextAlign(QCanvasPainter::TextAlign::Left);
        aPainter.setTextBaseline(QCanvasPainter::TextBaseline::Top);
    }
    for (const Label& label : aModel.labels) {
        QColor colour = aInk.content;
        if (!label.inActiveRange)
            colour.setAlphaF(kOutsideRangeFactor);
        aPainter.setFillStyle(colour);
        // The model's row is the box's top; Canvas2D's Top baseline puts the line box's
        // ascent there, which is where Qt's own drawText(rect) ink starts.
        aPainter.fillText(label.text, float(label.left), float(label.top));
    }

    // THE TEMPORARY PLAYBACK WINDOW: the wash, then the stems, then the pills - the
    // timeline's own order, so a merged pill wider than the window cannot hide the
    // stems' lower half.
    const PlayWindowOverlay& window = aModel.window;
    if (window.active) {
        if (!window.band.isEmpty()) {
            aPainter.setFillStyle(aInk.windowBand);
            aPainter.fillRect(QRectF(window.band));
        }
        aPainter.setFillStyle(aInk.windowEdge);
        if (!window.startStem.isEmpty())
            aPainter.fillRect(QRectF(window.startStem));
        if (!window.endStem.isEmpty())
            aPainter.fillRect(QRectF(window.endStem));

        const auto pill = [&aPainter, &aModel, &aInk](const Pill& aPill, bool aSquareLeft, bool aSquareRight) {
            if (!aPill.isValid())
                return;
            aPainter.setFillStyle(aInk.windowEdge);
            // A PILL IS ITS OWN PATH, and it is SMOOTH. Both halves are load-bearing and both
            // were missing: fill() does not reset the current path, so the ticks' subpaths were
            // still current and the roundRect was appended to them - the fill then inked none of
            // the pill's body (measured: the ruler showed the stems and the label text on the
            // bare band, with no pill at all, while the raster host's pills are one opaque
            // rounded rect). And the ticks leave antialiasing OFF, which a filled rounded rect
            // must state for itself or its corners are stair-stepped.
            aPainter.beginPath();
            aPainter.setAntialias(1);
            aPainter.roundRect(QRectF(aPill.rect), float(kPillRadius));
            aPainter.fill();
            aPainter.setAntialias(0);
            // The faces against the stems stay square, so the bracket joint reads
            // continuous (the QPainter renderer overdraws the same corners).
            const int r = kPillRadius;
            if (aSquareLeft) {
                aPainter.fillRect(
                    QRectF(aPill.rect.x(), aPill.rect.y(), r, r)
                );
                aPainter.fillRect(QRectF(aPill.rect.x(), aPill.rect.bottom() - r + 1, r, r));
            }
            if (aSquareRight) {
                aPainter.fillRect(QRectF(aPill.rect.right() - r + 1, aPill.rect.y(), r, r));
                aPainter.fillRect(QRectF(aPill.rect.right() - r + 1, aPill.rect.bottom() - r + 1, r, r));
            }
            aPainter.setFillStyle(aInk.windowText);
            aPainter.setTextAlign(QCanvasPainter::TextAlign::Left);
            aPainter.setTextBaseline(QCanvasPainter::TextBaseline::Top);
            aPainter.fillText(
                aPill.text, float(aPill.rect.left() + kPillPadding), float(aModel.band.top() + kNumberTop)
            );
        };
        if (window.merged.isValid())
            pill(window.merged, false, false);
        else {
            pill(window.start, true, false);
            pill(window.end, false, true);
        }
    }

    // THE PLAYHEAD'S PILL, LAST (the same order as the QPainter renderer): the model
    // reserved its column, so no number is drawn under it.
    if (aModel.playhead.isValid()) {
        aPainter.setFillStyle(aInk.playheadFill);
        // fill()/stroke() do NOT reset the current path (measured while porting the body's
        // markers: every shape was appended to the previous one's, 1923 px of phantom
        // differences). The ticks left their own subpaths current, so the pill states a
        // fresh path before adding its own.
        aPainter.beginPath();
        // The ANTI-ALIASED fill: the raster renderer's pill has a smooth corner ramp
        // (paintPill sets the hint itself), so the canvas states its own rather than
        // inheriting the ticks' "off".
        aPainter.setAntialias(1);
        aPainter.roundRect(QRectF(aModel.playhead.rect), float(kPillRadius));
        aPainter.fill();
        aPainter.setAntialias(0);
        aPainter.setFillStyle(aInk.playheadText);
        aPainter.setTextAlign(QCanvasPainter::TextAlign::Left);
        aPainter.setTextBaseline(QCanvasPainter::TextBaseline::Top);
        aPainter.fillText(
            aModel.playhead.text, float(aModel.playhead.rect.left() + kPillPadding),
            float(aModel.band.top() + kNumberTop)
        );
    }

    aPainter.restore();
}

} // namespace frameRuler
} // namespace gui
