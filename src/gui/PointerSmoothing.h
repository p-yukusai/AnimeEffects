#ifndef GUI_POINTERSMOOTHING_H
#define GUI_POINTERSMOOTHING_H

#include <QPointF>

#include <deque>

namespace gui {

// THE BRUSH'S PATH SMOOTHER: a moving average over the last D SCREEN PIXELS the pointer travelled.
//
// Why a window in PIXELS and not in samples: the pointer's quantisation is one screen pixel, so a
// screen-pixel window removes the same staircase at any zoom and any event rate. A sample-count
// window changes its strength with the pointer's rate (measured in the lab: the same N was +52 ms at
// 97 Hz and clamped at >=120 ms at 28 Hz), and a document-pixel window changes it with the zoom (one
// screen pixel is 1/zoom document pixels).
//
// The mean is the EXACT INTEGRAL of the reconstructed (linear) path over that length, so irregular
// sampling cannot bias it and the filter is a continuous function of arc length. The window trails
// the pen by D/2 px of path: at 100 px/s and D = 12 px that is ~60 ms (measured by cross-correlating
// the input and output traces in ~/dev/input-smoothing-lab), and at 20 px/s it is ~300 ms - the
// latency of a distance parameterisation is the speed's, not a constant.
//
// Measured on a 4-px staircase: the box has exact zeros, so D = 12 px (three periods) cancels the
// staircase outright - 0.000 px residual, against 59 risers for the raw path.
//
// The caller's duties:
//  - begin() at the press, so the stroke starts exactly under the pen (the window is truncated to
//    the path available so far, which anchors the start);
//  - step() once per pointer event, never with interpolated points (feeding interpolated samples
//    would flatten the reconstruction - the filter would only ever see collinear points);
//  - land the RAW release position after the last smoothed sample (ViewportInput does): the mean
//    ends D/2 px short of the pen, and a stroke must end under the nib.
class PointerSmoothing {
public:
    // The environment's window: `AE_POINTER_SMOOTH` in screen px, "0" (or a negative) disables it.
    // Unset means the measured default, 12 px.
    static double defaultWindowPx();

    explicit PointerSmoothing(double aWindowPx);
    PointerSmoothing();

    void setWindowPx(double aWindowPx);
    double windowPx() const { return mWindowPx; }
    bool isEnabled() const { return mWindowPx > 0.0; }
    // the trail of a steady drag, in screen px: the window's half length
    double lagPx() const { return mWindowPx * 0.5; }

    void begin(const QPointF& aPos);
    QPointF step(const QPointF& aPos);
    QPointF value() const { return mOut; }
    void reset();

private:
    struct Node {
        QPointF p;
        double s; // cumulative arc length at this point
    };

    QPointF averageAt(double aEnd) const;

    double mWindowPx;
    bool mInit = false;
    QPointF mPrev;
    QPointF mOut;
    double mLen = 0.0;
    std::deque<Node> mPoints;
};

} // namespace gui

#endif // GUI_POINTERSMOOTHING_H
