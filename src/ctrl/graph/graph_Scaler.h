#ifndef CTRL_GRAPH_SCALER_H
#define CTRL_GRAPH_SCALER_H

#include <QRect>
#include <QString>
#include "core/Frame.h"
#include "ctrl/graph/graph_ScrollBarGeometry.h"
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
    // THE FRAME RULER'S BAND, reserved ABOVE the plot: the 22px strip whose numbers
    // and ticks the timeline's own header carries (ctrl::TimeLineEditor::kHeaderHeight,
    // re-exported as gui::frameRuler::kBandHeight and static_asserted against this
    // constant there). It is reserved HERE, in the shared plot rect, rather than
    // carved out of the drawing layer's clip, because the plot rect is the one
    // geometry the curves, the keys, the handles, the value axis and the editor's
    // hit-test all read - so no ink can be clipped into the ruler's rows and no
    // axis can disagree about where the graph starts.
    static constexpr int kRulerBandHeight = 22;

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
    // Where curves are drawn: the rect minus the margins, the MEASURED label column
    // (see THE LABEL COLUMN above) and the RULER BAND's 22 rows at the top
    // (kRulerBandHeight). The band itself sits immediately above the returned rect
    // (GraphPanelModel publishes it at plot.top() - kRulerBandHeight), so the plot's
    // first row is the graph's, never the ruler's.
    //
    // THE RIGHT AND BOTTOM INSETS ARE THE SCROLL BARS' LANES: they are
    // kScrollBarReserve (12, the lane's own thickness) there and kMargin (+ the label
    // column / the ruler band) on the left and top, so the plot keeps its top-left
    // corner (and the ruler band above it) and its last row/column are the lane's
    // neighbours - see THE SCROLL BARS' LANES.
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

    // ---- THE SHARED WINDOW'S MAPPING ----
    //
    // While a timeline owns the frame axis, applyFrameWindow maps the timeline's own
    // visible window (TimeLineWidget::visibleFrameWindow: the frames at the timeline
    // viewport's two edges, FRACTIONAL) ACROSS THE PANEL: frame aLo lands on
    // panel.left(), frame aHi on panel.left() + panel.width(). It is deliberately not
    // the plot's width and it has no edge inset: the panel and the timeline's viewport
    // are the same columns of the same dock page (measured: page left 257, viewport
    // 638 for both), so this mapping makes the graph's frame coordinate the timeline's
    // PIXEL for that frame. That identity is the point - switching timeline -> graph
    // leaves the ruler, its ticks and its numbers exactly where they were, and both
    // views show one window at one zoom. The value column and the scroll bars are
    // reserves the plot gives up at its edges, not a second frame mapping: a frame
    // under them is still at the timeline's pixel, just hidden by chrome.
    //
    // The graph's own (unshared) axis keeps its origin inside the plot: pixelXOfFrame
    // reads plotRect().left() + mFrameOffset, and only the shared path writes an offset
    // that starts at the panel's left edge.

    // ---- THE SCROLL BARS' LANES ----
    //
    // The panel's two scroll bars (the frame axis' along the bottom, the value
    // axis' along the right) are the APPLICATION'S scroll bars, not a look of this
    // panel's own: src/gui/AppStyle.cpp's CC_ScrollBar draws a 12px lane whose
    // trough is INVISIBLE and a 4px, radius-2 pill across the slider's run
    // (theme::Colors::outline at rest, theme::Colors::hover when hot) - both
    // numbers from ctrl/graph/graph_ScrollBarGeometry.h, the one definition this
    // panel paints from as well. The timeline's
    // own bar (gui::TimeLineWidget's TimelineScrollBar) is that style on a lane flush
    // with its page's bottom and right edges, with no gap and no outer margin - so
    // the panel's lanes are FLUSH the same way: the horizontal lane is the panel's
    // last 12 rows, across its whole width, and the vertical lane the panel's last 12
    // columns, from the plot's first row down to the row above the horizontal lane
    // (the corner belongs to the horizontal bar, which is what keeps the two rects
    // disjoint).
    //
    // THE PLOT'S INSET IS THE LANE: plotRect() gives up exactly
    // kScrollBarReserve == kScrollBarThickness (12) at its right and bottom edges and
    // nothing else, so the plot's last row is the row above the horizontal lane's
    // first one and its last column the one left of the vertical lane's. The LEFT and
    // TOP insets are unchanged (kMargin + the label column, kMargin + the ruler
    // band), so the plot's top-left corner - and with it the band and the value
    // column - does not move. The reserve is unconditional (a PLOT-SHAPE decision):
    // the plot never moves because a bar appeared. With a 800x600 panel and a 68px
    // label column the numbers are:
    //   * plot:   (72, 26, 716, 562) - right/bottom = panel.right()/bottom() - 12
    //   * hTrack: (0, 588, 800, 12)  - the panel's last 12 rows, its whole width
    //   * vTrack: (788, 26, 12, 562) - the panel's last 12 columns, from plot.top() to
    //                                  the row above hTrack.top() (row 587)
    // The MEASURED reference is the timeline's own bar in the dock at 1440x900: its
    // page's last row is 494, its lane rows 483..494 and its pill rows 487..490 - the
    // panel's lane and pill rows are the same numbers, because the lanes are the same
    // rows of the same page.
    // These three are NOT this panel's numbers: they are the application scroll
    // bar's, defined once in ctrl/graph/graph_ScrollBarGeometry.h, where gui::AppStyle's
    // CC_ScrollBar reads them too. They are re-exported under the scaler's names
    // because this class is where the panel's bar geometry is read from - and the
    // pill that ink lands in is computed by that header's pill() from exactly
    // these lane/run rects, never by a second copy of the arithmetic here.
    static constexpr int kScrollBarThickness = scrollBar::kLaneThickness;
    static constexpr int kScrollBarPillThickness = scrollBar::kPillThickness;
    static constexpr int kScrollBarPillRadius = scrollBar::kPillRadius;
    static constexpr int kScrollBarReserve = kScrollBarThickness; // the plot's inset == the lane

    // One wheel notch over a LANE scrolls the window this many pixels times the
    // application's wheelScrollLines: the timeline's own step (gui::kVerticalStep = 24
    // content pixels, set as its scroll bar's singleStep), so a notch is 3 x 24 = 72px
    // on either bar here. The timeline's bar pages instead when Shift or Ctrl is held
    // (it has no modifier branch of its own: any nonzero delta is one page), so the
    // two agree for an unmodified notch only - the bar's own wheel is otherwise the
    // panel's.
    static constexpr int kScrollBarStepPixels = 24;

    // THE REFERENCE IS THE TIMELINE'S OWN HANDLE ARITHMETIC, not QStyle's slider
    // layout. gui::TimelineScrollBar paints from its model
    // (TimeLineWidget.cpp: handleRect): the handle is the VIEWPORT's own pixel span
    // over the UNION track, edge-rounded at both ends:
    //     fx(v)  = int((v - trackMin) / (trackMax - trackMin) * trackLength + 0.5)
    //     thumb  = [fx(viewLo), fx(viewHi)]   (width floored at 1px)
    // There is NO style minimum and NO one-pixel-short cap: those are Qt's QScrollBar
    // layout rules, and the panel must match the bar the user compares it with.
    //
    // The horizontal track is the project's frame range UNIONED with the visible
    // window, plus the timeline's trailing margin past the range's last frame
    // (kTimeLineMargin, 24px, in ctrl::TimeLineEditor). In frames that margin is
    // 24 / framePpf(); it is what makes the timeline's handle leave a margin after
    // the last frame when scrolled flush right. The vertical track is the shown
    // value extent unioned with the value window (no trailing margin).
    static constexpr int kScrollBarTrailingMargin = 24; // ctrl::TimeLineEditor::kTimeLineMargin

    // THE TWO SCROLL BARS' GEOMETRY, as one structure: the editor's press routing
    // and the model's drawing read THIS, so the bar a press grabs and the bar the
    // panel paints are one geometry (the rule the whole panel follows).
    //
    //   * hTrack: the panel's last 12 rows, its whole width (flush with the
    //     panel's bottom AND right edges, as the timeline's bar is).
    //   * vTrack: the panel's last 12 columns, from plot.top() to the row above
    //     hTrack (flush with the panel's right edge).
    //   * hThumb/vThumb: the timeline's handle over that track (see above): the
    //     window's span edge-rounded over the UNION track. The drawing layer paints
    //     the pill scrollBar::pill(hTrack, hThumb, Qt::Horizontal).rect /
    //     scrollBar::pill(vTrack, vThumb, Qt::Vertical).rect paints - the
    //     application's 4px, radius-2 pill centred in the lane across this run, the
    //     same function AppStyle's CC_ScrollBar paints from - and nothing else: the
    //     trough is invisible.
    //
    // THE TWO LANES ARE BOTH ALWAYSON IN THIS PANEL (the timeline has no vertical
    // bar of its own; see the second bullet):
    //   * hThumb is ALWAYS the handle - when the window covers the union track it
    //     spans the whole track (the timeline's AlwaysOn bar).
    //   * vThumb is ALWAYS the handle too: a value window that covers its track
    //     fills the vertical lane with the pill, exactly as the horizontal one does.
    //     (The timeline has no vertical bar to copy: TimeLineWidget builds a
    //     HORIZONTAL scroll bar only and its vertical position is the object tree, so
    //     the timeline's empty right lane is an absent bar, not a hidden one. The
    //     panel's value bar is its own, and a lane that disappears with the content in
    //     range is the defect this rule fixes.)
    //
    // A FULL-LENGTH RUN IS STILL A RUN. "There is nothing to travel" is NOT the
    // question a press asks: the timeline's own bar is grabbable at exactly this
    // state. TimeLineWidget::eventFilter checks TimelineScrollBar::handleRect (the
    // full lane) and calls beginScrollbarDrag, whose ratio is
    // `range / span` = trackSpan / laneLength - nonzero whenever the track has a
    // span, even when range == pageStep. A drag then moves the window by
    // dx * trackSpan/laneLength and the track re-unions around it; a wheel notch
    // scrolls by wheelScrollLines * kVerticalStep. So the grabbable question is
    // "does this lane have a RUN" (hHasRun/vHasRun), not "is the window narrower
    // than the track". This is the timeline's own precondition: its handleRect is
    // empty only when the track has no span at all.
    // Every rect is EMPTY on an empty plot (there is nowhere to lay a bar out).
    // They are CONTENT-free: what the window and the content are is the caller's
    // (see scrollBars).
    struct ScrollBars {
        QRect hTrack;
        QRect hThumb;
        QRect vTrack;
        QRect vThumb;
        // CONTENT UNITS PER LANE PIXEL for the grabbed axis. The timeline anchors its
        // thumb drag on this ratio at press (beginScrollbarDrag), which is what makes
        // the pill follow the pointer 1:1: a pointer delta dx moves the WINDOW by
        // dx * trackSpan/trackLength, so the handle's offset moves by exactly dx.
        // Nonzero exactly when the lane has a run.
        double hContentPerPixel = 0.0;
        double vContentPerPixel = 0.0;

        // True when there is no bar to draw and nothing to hit (an empty plot).
        bool isEmpty() const { return hTrack.isEmpty() && vTrack.isEmpty(); }

        // WHETHER THE LANE HAS A RUN TO GRAB - the press/drag precondition, and the
        // timeline's own (handleRect is empty only on a zero-span track). This is
        // deliberately not "the window is narrower than the track": at zero travel
        // the run IS the whole track, and a press on it still starts the drag
        // (beginScrollbarDrag is reached with a full-width handle too).
        bool hHasRun() const { return !hThumb.isEmpty(); }
        bool vHasRun() const { return !vThumb.isEmpty(); }
    };

    // The bars for THIS scaler's plot rect. The horizontal bar scrolls the FRAME
    // axis, so its content is the project's frame range (aFrameContentLo/Hi, in
    // frames) and its window is the frames the plot covers -
    // [frameAtPixel(plot.left()), frameAtPixel(plot.right() + 1)]. The vertical bar
    // scrolls the VALUE axis, so its content is the value extent of the shown
    // channels' keys (aValueContentLo/Hi, the extent ctrl::GraphEditor's value fit
    // uses) and its window is valueMin()..valueMax().
    //
    // THE TRACK IS THE UNION, as the timeline's is: min(contentLo, windowLo) ..
    // max(contentHi, windowHi), and the horizontal one also adds
    // kScrollBarTrailingMargin pixels past the range's last frame (expressed in
    // frames through framePpf()). A DEGENERATE content (lo == hi, i.e. no key at
    // all, or a project with no frame span) therefore has a track of just the
    // window: its handle fills the track exactly as a fully visible window does -
    // the full handle of BOTH lanes (see ScrollBars). The value axis is INVERTED along
    // its track - valueMax is at the track's top - which the layout accounts for, so
    // a window at high values draws its thumb at the top.
    ScrollBars scrollBars(
        double aFrameContentLo, double aFrameContentHi, double aValueContentLo, double aValueContentHi
    ) const;

    // Pixels per frame the mapping uses: the ladder's rung normally, the exact fit of
    // a pushed window while a timeline owns the frame axis (fractional - see
    // applyFrameWindow).
    double framePpf() const {
        return mFramePpf > 0.0 ? mFramePpf : static_cast<double>(mFrameScale.pixelsPerFrame());
    }

    // Maps the pushed window [aLo, aHi] (fractional frames) ACROSS THE PANEL:
    // frame aLo at panel.left(), frame aHi at panel.left() + panel.width(), rate
    // panel.width() / (aHi - aLo). This is the TIMELINE's own window and the
    // timeline's own columns, so pixelX() of a frame is the timeline's pixel for it
    // (see THE SHARED WINDOW'S MAPPING). False when there is no panel width or no
    // span to map.
    bool applyFrameWindow(double aLo, double aHi);

    // Back to the ladder's own rung: the panel's own frame axis.
    void clearFrameWindow() {
        mFramePpf = 0.0;
        mFrameWindowRect = QRect();
    }

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
    // Pans the frame axis by a pixel delta (fractional while the bar drag converts
    // through the track's own content-per-pixel ratio).
    void panFrame(double aPixelDeltaX);
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
    // The value axis' mapping, and its exact (sub-pixel) form: this is what the CURVES are drawn
    // with, because a curve's own y is what its edges are made of - a whole-pixel y would lock a
    // flat line's edges to the pixel grid and leave it with no antialiasing at all, while the frame
    // axis' exact mapping keeps a steep line's edges smooth. pixelY is for the integer chrome (the
    // value labels, the ticks, the 1px rules), whose placement the pixel gate pins exactly.
    double pixelYOfValue(float aValue) const;
    int pixelY(float aValue) const;
    float valueAt(int aPixelY) const;
    double valueAtPixel(double aPixelY) const;
    // Zooms the value axis by aFactor about aAnchorPixelY (aFactor > 1 zooms in).
    // Returns false when the resulting window would be degenerate.
    bool updateValueZoom(float aFactor, int aAnchorPixelY);
    // Pans the value axis by a pixel delta (positive moves the window down);
    // fractional while the bar drag converts through the track's own
    // content-per-pixel ratio.
    void panValue(double aPixelDeltaY);

private:
    // Measures axisLabelWidth()'s value. Called only while the memo is empty.
    int measureAxisLabelWidth() const;
    void invalidateAxisLabelWidth() { mMeasuredAxisLabelWidth = -1; }

    double plotTop() const;
    double plotHeight() const;

    ctrl::time::Scaler mFrameScale;
    QRect mRect;
    double mFrameOffset; // content pixel of frame 0, relative to plotRect().left().
                         // The shared mapping (applyFrameWindow) writes it so the
                         // window's first frame lands on panel.left(); the plot starts
                         // to the right of that column, so the offset is negative.
    // The shared window's mapping, when one is applied: pixels per frame as a
    // DOUBLE (see framePpf). 0 while the frame mapping is the ladder's own rung.
    double mFramePpf;
    // THE RECT THE SHARED MAPPING WAS FITTED OVER. applyFrameWindow maps the pushed
    // window across mRect AT THAT MOMENT, and mFramePpf/mFrameOffset are that fit's
    // numbers. A caller may hand this scaler another rect afterwards (buildGraphPanelModel
    // does, publishing the panel it was asked for), and the frames the mapping COVERS do
    // not move with it - so the horizontal bar's window has to be read over THIS rect,
    // not over whatever rect the caller set. Reading it over the new rect scales the
    // window's span by newWidth/fittedWidth: wider, and the bar reads as "the content
    // fits" and is removed while the panel is plainly showing a narrower window (the
    // measured "the horizontal bar is never there" report). Empty while the mapping is
    // the ladder's own.
    QRect mFrameWindowRect;
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
