#ifndef CTRL_GRAPH_SCALER_H
#define CTRL_GRAPH_SCALER_H

#include <QRect>
#include <QString>
#include "core/Frame.h"
#include "ctrl/time/time_Scaler.h"

namespace ctrl {
namespace graph {

// The graph panel's two axes: frames horizontally, values vertically.
//
// The frame axis DELEGATES to ctrl::time::Scaler, so the graph's frame grid, its
// zoom ladder and its tick classification are the timeline's - one convention
// for "how far apart are frames", not two. It is the graph's OWN instance
// (mFrameScale), seeded from the project's fps and frame range but never shared
// with the timeline's scaler, so fitting or zooming the graph's frame axis moves
// the graph's frame window and leaves the timeline's zoom exactly where it was.
// What this class adds is the panel's own origin (a free pixel offset, which is
// what lets a wheel zoom stay anchored on the cursor) and the value window.
//
// Frame 0 sits at plotRect().left() + frameOffset() by default; the offset is
// free, so panning can put any frame anywhere.
class Scaler {
public:
    // Panel layout. Public so the renderer and the hit-test share one geometry
    // instead of each carrying its own copy of the insets.
    static constexpr int kMargin = 4; // inset of the plot inside the panel

    // ---- THE LABEL COLUMN (a STABLE, format-derived reserve) ----
    //
    // The column left of the plot carries the value axis' numbers. Its width is
    // DERIVED, not a magic 46 - but derived from the FORMAT and the FONT alone,
    // never from the values a particular window happens to show.
    //
    // WHY STABLE AND NOT WINDOW-MEASURED: plotRect() insets the plot by this
    // width, so the plot's left edge (and with it every frame coordinate in the
    // panel) is a function of it. A reserve measured from the window's own three
    // labels MOVES the moment a gesture changes which of them is widest - a
    // middle-drag pan of exactly 40px moved plot.left by 1px and the frame
    // coordinate by 41 instead of 40 (measured before this rule existed). The
    // value column is chrome: it must not move because the numbers did.
    //
    // THE APPLICATION'S OWN PRECEDENT is a format-derived reserve too: the
    // timeline's ruler does NOT measure its labels - src/gui/TimeRuler.cpp:103
    // reserves `const int numberWidth = 6` per character and multiplies by the
    // string's length.
    //
    // THE RULE: axisLabelWidth() measures, once, the widest label the value
    // format can WRITE - one candidate per shape valueAxisText() can emit, both
    // signs - and adds the box's right inset (kAxisLabelPad) and a small left gap
    // (kAxisLabelTextPad), then clamps into [kAxisLabelWidth, kMaxAxisLabelWidth].
    // The box a label goes in is `column.width() - kAxisLabelPad` wide and the
    // drawing layer right-aligns the text in it, so
    // `column >= advance(text) + kAxisLabelPad` is exactly the condition that
    // keeps a number inside the column, and the reserve above satisfies it for
    // every label the format can produce at the UI font.
    //
    // WHY IT LIVES HERE AND NOT IN THE MODEL: plotRect() insets by this width, so
    // the MODEL's plot and the EDITOR's hit-test geometry both come through this
    // one function. A width the model alone knew would move the drawn plot away
    // from the grabbed one. (The model publishes valueColumn at this same width,
    // so the column and the plot cannot disagree either.)
    //
    // THE NUMBERS, MEASURED (QFontMetrics(QFont()) on this machine, family
    // "Open Sans", height 17px; the format is 'g' with four significant digits,
    // so a label is at most 10 characters - sign + "d.ddd" + 'e' + sign + 2
    // digits, or sign + "0." + 3 zeros + 4 digits):
    //     "0" 7   "1" 7   "9" 7   "-" 4   "." 3   "e" 7   "+" 7
    //     "0.0009999" 58   "-0.0009999" 62        (the plain form's widest)
    //     "3.403e+38" 58   "-3.403e+38" 62        (the exponent form's widest)
    //     "-1.401e-45" 59  "1.235e+04" 58  "-1e+05" 38  "-1234" 31
    // A sweep of 'g'/4 over the float range (extremes, denormals, powers of ten,
    // a dense run of mantissas) confirms 62px as the widest string the format can
    // write, and the two 62px candidates above are that sweep's winners. Reserve
    // = 62 + 4 + 2 = 68px, well inside the 96px ceiling, and - the point of the
    // rule - THE SAME 68px whatever the window shows.
    //
    // THE CEILING is a PANEL-PROTECTION bound (the plot must keep its share of a
    // narrow viewport, and a pathological font must not hand the column the whole
    // panel), not a clipping rule: the value axis' own labels never reach it at
    // the UI font. THE FLOOR keeps the column from ever being narrower than the
    // 46px the panel used to reserve. And if a font were so wide that the
    // requirement passed the ceiling, the model DROPS a label that does not fit
    // its box rather than publishing it overflowing - the frame axis' own rule -
    // so "no published label exceeds its box" holds unconditionally, not just at
    // this font.
    // The column's floor: never narrower than the 46px the panel used to reserve.
    static constexpr int kAxisLabelWidth = 46;
    // The right inset of a value label's box inside the column: a right-aligned
    // number must not touch the curves it names (the deleted painter renderer's
    // kAxisLabelPad, kept as the model's convention).
    static constexpr int kAxisLabelPad = 4;
    // The gap between the column's left edge and the widest number's left edge.
    static constexpr int kAxisLabelTextPad = 2;
    // The column's ceiling. ~2.1x the floor and 1.4x the reserve a 'g'/4 label
    // asks for at the UI font (68px, measured above).
    static constexpr int kMaxAxisLabelWidth = 96;

    Scaler();

    // The panel rect, in viewport pixels.
    void setRect(const QRect& aRect);
    const QRect& rect() const { return mRect; }
    // Where curves are drawn: the rect minus the margins and the MEASURED label
    // column (see THE LABEL COLUMN above).
    QRect plotRect() const;

    // The width of the left label column: the reserve described in THE LABEL
    // COLUMN above - the widest label the value FORMAT can write, measured with
    // QFontMetrics over the default UI font, plus the box inset and the text gap,
    // clamped into [kAxisLabelWidth, kMaxAxisLabelWidth]. It is a function of the
    // font and the format ALONE: it does not read the value window, so a pan or a
    // wheel step cannot move the plot's left edge. plotRect() insets by this
    // width, so it is part of the panel's shared geometry and not the model's
    // private arithmetic. Memoized; the memo is dropped when the value window
    // moves, which is simply the cheapest moment to re-read the application font.
    int axisLabelWidth() const;

    // The value axis' own number format: 'g' with four significant digits (the
    // deleted painter renderer's valueText()). It lives beside the axis because
    // the column's WIDTH is the width of the strings this produces - the rule
    // that sizes the column and the rule that writes the label are one rule, or
    // the column would be sized for text nobody draws.
    static QString valueAxisText(float aValue);

    // --- frame axis (ctrl::time::Scaler owns the ladder) ---
    ctrl::time::Scaler& frameScale() { return mFrameScale; }
    const ctrl::time::Scaler& frameScale() const { return mFrameScale; }
    int pixelX(core::Frame aFrame) const;
    core::Frame frameAt(int aPixelX) const;
    double frameAtPixel(double aPixelX) const;
    double pixelXOfFrame(double aFrame) const;
    // Zooms the frame axis by a wheel delta so the frame under aAnchorPixelX
    // stays under it (the timeline's anchoring rule). Returns false when the
    // zoom is already clamped at the floor or ceiling and nothing moved.
    bool updateFrameZoom(int aWheelDelta, int aAnchorPixelX);
    // Pans the frame axis by a pixel delta.
    void panFrame(int aPixelDeltaX);
    // Fits the frame window so every frame in [aLo,aHi] lands inside
    // plotRect(), with a small pad at both ends. This is the graph's OWN frame
    // axis - its own ctrl::time::Scaler instance - so the fit moves the graph's
    // frame zoom rung and origin and leaves the timeline untouched. The rung is
    // the largest the axis can show the span at; a span wider than the axis'
    // floor (1 px/frame) allows is centred at that floor rather than refused.
    // Does nothing when the plot has no width.
    void fitFrameAxis(core::Frame aLo, core::Frame aHi);

    // --- value axis ---
    void setValueWindow(float aMin, float aMax);
    float valueMin() const { return mValueMin; }
    float valueMax() const { return mValueMax; }
    // Fits [aMin,aMax] with padding on both sides, and widens a degenerate span
    // so an all-constant channel still has a usable scale.
    void fitValueWindow(float aMin, float aMax);
    int pixelY(float aValue) const;
    float valueAt(int aPixelY) const;
    double valueAtPixel(double aPixelY) const;
    // Zooms the value axis by aFactor about aAnchorPixelY (aFactor > 1 zooms in).
    // Returns false when the resulting window would be degenerate.
    bool updateValueZoom(float aFactor, int aAnchorPixelY);
    // Pans the value axis by a pixel delta (positive moves the window down).
    void panValue(int aPixelDeltaY);

private:
    // Measures axisLabelWidth()'s value. Called only while the memo is empty.
    int measureAxisLabelWidth() const;
    void invalidateAxisLabelWidth() { mMeasuredAxisLabelWidth = -1; }

    double plotTop() const;
    double plotHeight() const;

    ctrl::time::Scaler mFrameScale;
    QRect mRect;
    double mFrameOffset; // content pixel of frame 0, relative to plotRect().left()
    float mValueMin;
    float mValueMax;
    // Memo of axisLabelWidth(), -1 while unmeasured. plotRect() is on the pixel
    // mapping's hot path (every sample of every polyline goes through pixelX/
    // pixelY), so the measurement must not run per call.
    mutable int mMeasuredAxisLabelWidth;
};

} // namespace graph
} // namespace ctrl

#endif // CTRL_GRAPH_SCALER_H
