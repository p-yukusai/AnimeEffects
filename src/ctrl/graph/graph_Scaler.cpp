#include "ctrl/graph/graph_Scaler.h"

#include <QFont>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
// A window whose span falls below this fraction of the value it shows has no
// resolution left: at float precision the pixel mapping would be rounding
// noise, and the inverse would amplify it into a curve that flickers.
constexpr double kMinRelativeSpan = 1e-6;

// The values whose 'g'/4 forms are the WIDEST the value format can write - one
// candidate per shape it can emit, both signs where the sign changes the width.
// The format is 'g' with four significant digits, so a label is at most 10
// characters: sign + "d.ddd" + 'e' + sign + 2 digits, or sign + "0." + 3 zeros +
// 4 digits. These are the two 10-character shapes (plus the round-number forms
// that fill the exponent), and the max over them is the reserve's basis - a
// property of the FORMAT, so it does not move when the value window does (see
// THE LABEL COLUMN in the header). Each is stated with the text it produces at
// this font, measured:
//     -3.40282e+38 -> "-3.403e+38" 62px   (exponent shape, widest)
//     -0.0009999   -> "-0.0009999" 62px   (plain shape, widest)
//      1.4013e-45  -> "1.401e-45"  55px
//     -1.4013e-45  -> "-1.401e-45" 59px
//      12345.6     -> "1.235e+04"  58px
//      100000      -> "1e+05"      34px
//     -1234        -> "-1234"      31px
// A sweep of the whole float range finds 62px as the true widest, so these
// candidates are that sweep's winners; a font whose glyph advances differ more
// than this set can express is still covered - the model drops a label that does
// not fit its box rather than overflowing it (graph_Scaler.h's ceiling).
constexpr float kValueLabelExtremes[] = {
    -std::numeric_limits<float>::max(), // "-3.403e+38": exponent shape, sign + 'e' + '+' (the widest)
    -0.0009999f,                        // "-0.0009999": plain shape, sign + "0." + 3 zeros + 4 digits
    -std::numeric_limits<float>::denorm_min(), // "-1.401e-45": exponent shape with a negative exponent
    std::numeric_limits<float>::max(),         // "3.403e+38"
    std::numeric_limits<float>::denorm_min(),  // "1.401e-45"
    12345.6f,                                  // "1.235e+04" - the shape a mid-size value takes
    100000.0f,                                 // "1e+05"
    -100000.0f,                                // "-1e+05"
    -1234.0f,                                  // "-1234" - the widest plain integer form
};
} // namespace

namespace ctrl {
namespace graph {

    //-------------------------------------------------------------------------------------------------
    QString Scaler::valueAxisText(float aValue) {
        // 'g' with four significant digits: a narrow window keeps its digits, a
        // large value loses its trailing zeros. The column's width is measured
        // off these strings, so this and the labels must stay one function.
        return QString::number(static_cast<double>(aValue), 'g', 4);
    }

    //-------------------------------------------------------------------------------------------------
    int Scaler::measureAxisLabelWidth() const {
        // The default UI font, exactly as the model's label boxes are measured
        // (GraphPanelModel.cpp: QFontMetrics{QFont()}). The model has no painter
        // by design, so both halves of the panel agree on this font.
        const QFontMetrics metrics{QFont()};

        // The widest label the format can WRITE - not the widest one this window
        // happens to show: the column insets the plot, and the plot must not move
        // when the numbers do (THE LABEL COLUMN in the header). kValueLabelExtremes
        // is one candidate per shape the format can emit; the header lists the
        // measured widths and the float-range sweep behind them.
        int widest = 0;
        for (const float value : kValueLabelExtremes)
            widest = std::max(widest, metrics.horizontalAdvance(valueAxisText(value)));

        // The box's right inset plus a left gap, clamped into the column's floor
        // and ceiling - see THE LABEL COLUMN in the header for both bounds, the
        // measured text widths they come from, and why the width lives on the
        // scaler at all (plotRect() is the editor's geometry too).
        return std::clamp(widest + kAxisLabelPad + kAxisLabelTextPad, kAxisLabelWidth, kMaxAxisLabelWidth);
    }

    int Scaler::axisLabelWidth() const {
        if (mMeasuredAxisLabelWidth < 0)
            mMeasuredAxisLabelWidth = measureAxisLabelWidth();
        return mMeasuredAxisLabelWidth;
    }

    //-------------------------------------------------------------------------------------------------
    // The value window is seeded non-degenerate (0..1): every mapping in the
    // class assumes a positive span, and the setters below are what keep that
    // true.
    Scaler::Scaler():
        mFrameScale(),
        mRect(),
        mFrameOffset(0.0),
        mFramePpf(0.0),
        mValueMin(0.0f),
        mValueMax(1.0f),
        mMeasuredAxisLabelWidth(-1) {}

    //-------------------------------------------------------------------------------------------------
    void Scaler::setRect(const QRect& aRect) { mRect = aRect; }

    QRect Scaler::plotRect() const {
        // The label column is a left-only inset: it is where the value numbers
        // sit, so the curves start to its right. Its width is MEASURED from the
        // labels this window will draw (axisLabelWidth(), memoized) rather than
        // reserved at a fixed 46px, so a number can never be cut off by the
        // column while the plot keeps as much as the labels leave it.
        // The frame offset is NOT baked in here - it lives on the frame mapping, so this
        // stays a pure function of the panel geometry and the value window.
        //
        // THE TOP INSET IS THE RULER BAND. The panel's frame ruler is a SECTION above
        // the graph (the timeline's own arrangement: its surface starts below the
        // 22px TimeRuler strip), so its rows are taken out of the plot here - in the
        // one rect the curves, the keys, the handles, both axes and the editor's
        // hit-test share. Nothing that is clipped to the plot can reach the ruler.
        // THE RIGHT AND BOTTOM INSETS ARE THE SCROLL BARS' LANES (THE SCROLL BARS'
        // LANES in the header): the plot gives up exactly the lane - 12px, the bar's
        // own thickness - at those two edges, because the lanes are the panel's last
        // 12 rows and columns. So the plot's right edge is the lane's left edge and
        // its bottom edge the lane's top edge; there is no gap and no extra margin
        // (the timeline's bar has none either). The inset is unconditional: the plot
        // never moves because a bar appeared, and the bars never move because the
        // plot's content did.
        return mRect.adjusted(
            kMargin + axisLabelWidth(), kMargin + kRulerBandHeight, -kScrollBarReserve,
            -kScrollBarReserve
        );
    }

    //-------------------------------------------------------------------------------------------------
    // THE TIMELINE'S HANDLE, in the track's own pixels.
    //
    // gui::TimelineScrollBar does not use QScrollBar's value-driven slider layout:
    // projectHorizontalScrollBar builds a UNION track (the project's range never
    // clamps the view) and paintEvent draws handleRect(), which maps the viewport's
    // two edges over the FULL track and rounds each edge:
    //
    //     fx(v) = int((v - trackMin) / (trackMax - trackMin) * trackLength + 0.5)
    //     thumb = [fx(viewLo), fx(viewHi)]           (width floored at 1px)
    //
    // There is no style minimum and no one-pixel-short cap here - those are Qt's
    // QScrollBar layout rules, and the panel's bar is compared with the timeline's,
    // not with a stock QScrollBar. aInverted reads the value axis in TRACK order
    // (valueMax at the top) so both axes share this one arithmetic.
    //
    // The track is the UNION of content and window: a window panned off the content
    // still lies inside its own track, so the handle is flush with the edge it pokes
    // through. `contentPerPixel` is the ratio the drag anchors on: a pointer delta dx
    // moves the window by dx * trackSpan/trackLength, which moves the handle's offset
    // by exactly dx - the 1:1 drag the timeline has. It is nonzero whenever a run
    // exists, including a full-length run: the timeline's beginScrollbarDrag is
    // anchored on `range / span`, which stays positive at range == pageStep.
    struct BarLayout {
        QRect thumb;
        double contentPerPixel = 0.0;
    };

    static BarLayout timelineHandle(
        const QRect& aTrack, bool aHorizontal, bool aInverted, double aContentLo, double aContentHi,
        double aWindowLo, double aWindowHi
    ) {
        BarLayout out;
        if (aTrack.isEmpty())
            return out;
        const int trackLength = aHorizontal ? aTrack.width() : aTrack.height();
        if (trackLength <= 0)
            return out;

        const double windowLow = aInverted ? -std::max(aWindowLo, aWindowHi) : std::min(aWindowLo, aWindowHi);
        const double windowHigh = aInverted ? -std::min(aWindowLo, aWindowHi) : std::max(aWindowLo, aWindowHi);
        const double contentLo = aInverted ? -std::max(aContentLo, aContentHi) : std::min(aContentLo, aContentHi);
        const double contentHi = aInverted ? -std::min(aContentLo, aContentHi) : std::max(aContentLo, aContentHi);
        const double pageSpan = windowHigh - windowLow;
        if (!(pageSpan > 0.0))
            return out; // a degenerate window is "nothing to lay out"

        // THE UNION TRACK, the timeline's range: content and window, whichever
        // reaches further at each end.
        const double trackLo = std::min(contentLo, windowLow);
        const double trackHi = std::max(contentHi, windowHigh);
        const double trackSpan = trackHi - trackLo;
        if (!(trackSpan > 0.0))
            return out;

        // handleRect's edge rounding, over the FULL track (no groove inset).
        const auto edge = [&](double aValue) {
            return int((aValue - trackLo) / trackSpan * double(trackLength) + 0.5);
        };
        const int start = edge(windowLow);
        const int end = edge(windowHigh);
        const int thumbLength = std::max(end - start, 1);
        out.thumb = aHorizontal
            ? QRect(aTrack.left() + start, aTrack.top(), thumbLength, aTrack.height())
            : QRect(aTrack.left(), aTrack.top() + start, aTrack.width(), thumbLength);
        out.contentPerPixel = trackSpan / double(trackLength);
        return out;
    }

    Scaler::ScrollBars Scaler::scrollBars(
        double aFrameContentLo, double aFrameContentHi, double aValueContentLo, double aValueContentHi
    ) const {
        ScrollBars bars;
        const QRect plot = plotRect();
        if (plot.isEmpty())
            return bars; // nowhere to lay a bar out

        // The LANES, flush with the panel's edges (THE SCROLL BARS' LANES in the
        // header): the horizontal one is the panel's last 12 rows over its whole
        // width - its right edge IS the panel's right edge - and the vertical one the
        // panel's last 12 columns from the plot's first row to the row above the
        // horizontal lane. The corner therefore belongs to the horizontal bar,
        // exactly as a two-bar scroll area's does, and the two rects never overlap.
        const int lane = kScrollBarThickness;
        bars.hTrack = QRect(mRect.left(), mRect.bottom() - lane + 1, mRect.width(), lane);
        bars.vTrack = QRect(
            mRect.right() - lane + 1, plot.top(), lane, mRect.bottom() - lane - plot.top() + 1
        );

        // The windows: the frames the view covers and the value window this scaler
        // holds. WHICH COLUMNS the frame window spans is the mapping's own answer: the
        // shared window spans the rect applyFrameWindow was fitted over, while the
        // graph's own axis fills the plot. Using the plot in the shared case would make
        // the run shorter than the window actually shown - the timeline's slider
        // measures its viewport, and so must this one - and using the CALLER's rect
        // instead of the fitted one (mFrameWindowRect) scales the window by the two
        // widths' ratio, which is how a narrower window on screen came out as "no
        // travel" and no bar at all.
        //
        // THE TRAILING MARGIN is the timeline's: frameEnd = frameEndPixel() +
        // kTimeLineMargin, so the bar's content reaches one margin past the last
        // frame's tick. It is a PIXEL margin (24), converted here to frames at the
        // axis' own rate - which is what makes the panel's denominator the timeline's,
        // not the raw project span.
        const QRect frameWindow =
            (mFramePpf > 0.0 && !mFrameWindowRect.isEmpty()) ? mFrameWindowRect : plot;
        const double marginFrames = double(kScrollBarTrailingMargin) / framePpf();
        const BarLayout horizontal = timelineHandle(
            bars.hTrack, true, false, aFrameContentLo, aFrameContentHi + marginFrames,
            frameAtPixel(frameWindow.left()), frameAtPixel(frameWindow.left() + frameWindow.width())
        );
        bars.hThumb = horizontal.thumb;
        bars.hContentPerPixel = horizontal.contentPerPixel;

        // THE VERTICAL LANE IS ALWAYS DRAWN, like the horizontal one: the run is the
        // handle over the union track, and a window that covers its content fills the
        // lane exactly as the horizontal bar does there. There is no timeline vertical
        // bar to mirror: TimeLineWidget builds a HORIZONTAL TimelineScrollBar only
        // (its vertical position belongs to the object tree), so the timeline's empty
        // right lane is an ABSENT bar, not a hidden one. The panel's value bar is its
        // own, and a bar that vanished with the content in range is the defect this
        // rule fixes.
        // A full-length run is GRABBABLE: the run (vThumb) is non-empty, and the
        // press/drag ratio is the union track's own (see hHasRun in the header).
        // There is no travel flag here to make the full run inert - the timeline's
        // full-width handle is not inert either.
        const BarLayout vertical = timelineHandle(
            bars.vTrack, false, true, aValueContentLo, aValueContentHi, static_cast<double>(valueMin()),
            static_cast<double>(valueMax())
        );
        bars.vThumb = vertical.thumb;
        bars.vContentPerPixel = vertical.contentPerPixel;
        return bars;
    }

    //-------------------------------------------------------------------------------------------------
    // The frame axis is linear in pixels-per-frame. The ladder of that rate belongs
    // to ctrl::time::Scaler; the rate the MAPPING uses is framePpf(), and the origin
    // is mFrameOffset, a free content pixel kept in doubles so an anchored zoom
    // accumulates no integer drift.
    double Scaler::pixelXOfFrame(double aFrame) const {
        return plotRect().left() + mFrameOffset + framePpf() * aFrame;
    }

    int Scaler::pixelX(core::Frame aFrame) const {
        // getDecimal(): a key may sit on a sub-frame position; only the pixel rounds.
        return static_cast<int>(std::lround(pixelXOfFrame(aFrame.getDecimal())));
    }

    double Scaler::frameAtPixel(double aPixelX) const {
        return (aPixelX - plotRect().left() - mFrameOffset) / framePpf();
    }

    core::Frame Scaler::frameAt(int aPixelX) const {
        return core::Frame(static_cast<int>(std::lround(frameAtPixel(static_cast<double>(aPixelX)))));
    }

    //-------------------------------------------------------------------------------------------------
    bool Scaler::applyFrameWindow(double aLo, double aHi) {
        // THE TIMELINE'S WINDOW ACROSS THE TIMELINE'S COLUMNS. The pushed window is
        // TimeLineWidget::visibleFrameWindow: the frames at the timeline viewport's
        // two edges, and the panel covers the same columns of the same dock page.
        // Mapping it across the PANEL (not the plot, and with no inset) therefore
        // gives every frame the pixel the timeline gives it - the identity a tab
        // switch depends on. The value column and the scroll bars hide the plot's
        // outermost columns; they do not move the mapping.
        const double panelWidth = static_cast<double>(mRect.width());
        if (!(panelWidth > 0.0) || !(aHi > aLo))
            return false;

        mFramePpf = panelWidth / (aHi - aLo);
        // The fit's own rect: scrollBars() reads the window over THIS one (see
        // mFrameWindowRect), so a caller that sets another rect cannot turn the run
        // empty by widening the columns it is measured across.
        mFrameWindowRect = mRect;
        // Frame aLo at panel.left(): pixelXOfFrame() reads
        // plotRect().left() + mFrameOffset + ppf * frame, so the offset carries the
        // distance from the plot's left edge back to the panel's.
        mFrameOffset = static_cast<double>(mRect.left() - plotRect().left()) - mFramePpf * aLo;

        // The ladder now only picks the tick classification's density; positions come from the
        // mapping (pixelX), so a fractional rate cannot desync them. indexForRate inverts the
        // ladder, which has room on both sides of its linear core now.
        mFrameScale.setIndex(mFrameScale.indexForRate(mFramePpf));
        return true;
    }

    //-------------------------------------------------------------------------------------------------
    bool Scaler::updateFrameZoom(int aWheelDelta, int aAnchorPixelX) {
        // The anchor frame is captured sub-pixel (frameAtPixel, never frameAt):
        // re-anchoring through a rounded frame would move the content by up to
        // half a frame, which at the ladder's ceiling is 8 px of drift for a
        // single notch. ctrl::TimeLineEditor::anchorPixelAfterScale is the same
        // rule - scale the content coordinate exactly, round only the final
        // pixel - and this is its graph-side equivalent with the rounding
        // deferred entirely into the returned int.
        const double anchorX = static_cast<double>(aAnchorPixelX);
        const double anchorFrame = frameAtPixel(anchorX);
        if (!mFrameScale.update(aWheelDelta))
            return false; // clamped at the ladder's floor or ceiling: nothing moved to re-anchor
        // framePpf() and not the ladder: this is the panel's OWN frame axis (a
        // timeline that owns it delegates the notch instead of arriving here), so
        // mFramePpf is 0 and the two are one value - reading the mapping's rate
        // keeps that true if the two ever stop being the same thing.
        mFrameOffset = anchorX - plotRect().left() - framePpf() * anchorFrame;
        return true;
    }

    void Scaler::panFrame(double aPixelDeltaX) { mFrameOffset += aPixelDeltaX; }

    //-------------------------------------------------------------------------------------------------
    void Scaler::fitFrameAxis(core::Frame aLo, core::Frame aHi) {
        const QRect plot = plotRect();
        if (!(plot.width() > 0))
            return; // no plot to fit into (an unlaid-out scale)

        double lo = std::min(static_cast<double>(aLo.getDecimal()), static_cast<double>(aHi.getDecimal()));
        double hi = std::max(static_cast<double>(aLo.getDecimal()), static_cast<double>(aHi.getDecimal()));
        // A small pad so an extreme key is drawn inside the plot instead of on
        // its border; at least one frame so a single-frame span still has a
        // positive width to divide by.
        const double pad = std::max(1.0, (hi - lo) * 0.05);
        lo -= pad;
        hi += pad;
        const double span = hi - lo;

        // The rung that still fits the span - the sparsest one whose rate is <= the plot's own
        // rate for it, which is what indexForRate answers (it is the ladder's inverse now that
        // the ladder has room on both sides of its linear core). update() clamps to the ladder's
        // ends, so a span too wide or too narrow for it lands on the nearest rung there is.
        const double wantedRate = static_cast<double>(plot.width()) / span;
        mFrameScale.setIndex(mFrameScale.indexForRate(wantedRate));

        // Centre the padded span in the plot. mFrameOffset is the content pixel
        // of frame 0 relative to plotRect().left(), which is exactly where
        // pixelXOfFrame() reads it back from.
        const double ppf = mFrameScale.pixelsPerFrame();
        mFrameOffset = 0.5 * (static_cast<double>(plot.width()) - span * ppf) - ppf * lo;
    }

    //-------------------------------------------------------------------------------------------------
    // The value axis is linear and inverted: the top of the plot is valueMax,
    // its bottom end is valueMin. Only the two bounds are stored, so the
    // mapping is a pure function of them and a zoom can hold the value under
    // the cursor exactly.
    void Scaler::setValueWindow(float aMin, float aMax) {
        if (!std::isfinite(aMin) || !std::isfinite(aMax))
            return; // a NaN or Inf bound is not a window: keep showing the previous one
        const float lo = std::min(aMin, aMax);
        const float hi = std::max(aMin, aMax);
        // A zero-height window has no inverse (and no pixel mapping that means
        // anything), so a degenerate request keeps the previous window rather
        // than blanking the axis. fitValueWindow is what widens a real
        // constant channel into a usable one.
        if (!(hi > lo))
            return;
        mValueMin = lo;
        mValueMax = hi;
        // The reserve does NOT depend on the window (THE LABEL COLUMN in the
        // header), so this drop is not what keeps the column stable - it is the
        // cheapest moment to re-read the application font.
        invalidateAxisLabelWidth();
    }

    void Scaler::fitValueWindow(float aMin, float aMax) {
        if (!std::isfinite(aMin) || !std::isfinite(aMax))
            return; // fitting a NaN has no meaning; keep the window we have

        double lo = std::min(aMin, aMax);
        double hi = std::max(aMin, aMax);

        const auto widenConstant = [&lo, &hi]() {
            // Constant channel: centre it with room to breathe, or the curve
            // would be glued to the edge of a zero-height window and its keys
            // would be unreachable. The padding scales with the value so 1000
            // and 0.001 both stay readable, and never shrinks below 1.
            const double value = 0.5 * (lo + hi);
            const double pad = std::max(1.0, std::fabs(value) * 0.1);
            lo = value - pad;
            hi = value + pad;
        };

        // The window is stored in float, so bounds that round to one float are a
        // constant channel as far as the axis can tell - and a degenerate window
        // would leave pixelY with nothing to divide by. Test the STORED bounds,
        // not the span: two values can differ by a tiny double span and still
        // round to the same pair of floats.
        if (!(static_cast<float>(lo) < static_cast<float>(hi))) {
            widenConstant();
        } else {
            // 5% of the span on each side, so an extreme key is drawn inside
            // the plot instead of on its border.
            const double pad = (hi - lo) * 0.05;
            lo -= pad;
            hi += pad;
            // A near-constant span can survive the double pad and still round
            // to one float pair; widen THAT case too.
            if (!(static_cast<float>(lo) < static_cast<float>(hi))) {
                widenConstant();
            }
        }
        setValueWindow(static_cast<float>(lo), static_cast<float>(hi));
    }

    //-------------------------------------------------------------------------------------------------
    double Scaler::pixelYOfValue(float aValue) const {
        const double height = plotHeight();
        const double span = static_cast<double>(mValueMax) - mValueMin;
        if (!(height > 0.0) || !(span > 0.0))
            return plotTop(); // no vertical span to map through
        return plotTop() + (static_cast<double>(mValueMax) - static_cast<double>(aValue)) / span * height;
    }

    int Scaler::pixelY(float aValue) const { return static_cast<int>(std::lround(pixelYOfValue(aValue))); }

    double Scaler::valueAtPixel(double aPixelY) const {
        const double height = plotHeight();
        const double span = static_cast<double>(mValueMax) - mValueMin;
        if (!(height > 0.0) || !(span > 0.0))
            return 0.5 * (static_cast<double>(mValueMin) + mValueMax); // degenerate plot: no value to read
        return mValueMax - (aPixelY - plotTop()) / height * span;
    }

    float Scaler::valueAt(int aPixelY) const { return static_cast<float>(valueAtPixel(aPixelY)); }

    //-------------------------------------------------------------------------------------------------
    bool Scaler::updateValueZoom(float aFactor, int aAnchorPixelY) {
        if (!std::isfinite(aFactor) || !(aFactor > 0.f))
            return false; // a zero, negative or NaN factor is not a zoom
        const double span = static_cast<double>(mValueMax) - mValueMin;
        if (!(span > 0.0))
            return false; // unreachable while the setters hold, but the mapping depends on it

        // Keep the value under the cursor fixed: split the span about that
        // value, so the anchor is preserved exactly and nothing else in the
        // window moves relative to it.
        const double anchorValue = valueAtPixel(aAnchorPixelY);
        const double newSpan = span / aFactor;
        if (!(newSpan > kMinRelativeSpan * std::max(1.0, std::fabs(anchorValue))))
            return false; // the axis would have no resolution left around the anchor
        const double lo = anchorValue - (anchorValue - mValueMin) / aFactor;
        const double hi = anchorValue + (mValueMax - anchorValue) / aFactor;
        if (!std::isfinite(lo) || !std::isfinite(hi) || !(hi > lo))
            return false; // the window overflowed its storage: leave the axis alone
        mValueMin = static_cast<float>(lo);
        mValueMax = static_cast<float>(hi);
        // The reserve is window-independent (THE LABEL COLUMN in the header);
        // the memo is dropped only to re-read the application font.
        invalidateAxisLabelWidth();
        return true;
    }

    //-------------------------------------------------------------------------------------------------
    void Scaler::panValue(double aPixelDeltaY) {
        const double height = plotHeight();
        const double span = static_cast<double>(mValueMax) - mValueMin;
        if (!(height > 0.0) || !(span > 0.0))
            return; // nothing to pan
        // Positive delta drags the content down the panel, which reveals
        // higher values, so the window moves up the value axis by however much
        // those pixels are worth. setValueWindow keeps the result ordered and
        // finite (a pan past the float range is refused there rather than
        // producing an infinity).
        const double delta = aPixelDeltaY / height * span;
        setValueWindow(static_cast<float>(mValueMin + delta), static_cast<float>(mValueMax + delta));
    }

    //-------------------------------------------------------------------------------------------------
    // The plot's vertical pixel geometry, in doubles. Every value mapping goes
    // through these two so the int rect is converted in one place.
    double Scaler::plotTop() const { return plotRect().top(); }

    double Scaler::plotHeight() const { return plotRect().height(); }

} // namespace graph
} // namespace ctrl
