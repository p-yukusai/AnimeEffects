#ifndef CTRL_TIME_SCALER_H
#define CTRL_TIME_SCALER_H

#include <array>
#include <vector>

namespace ctrl {
namespace time {

    class Scaler {
    public:
        // Minimum on-screen distance (px) between consecutive numbers of a
        // tier before the scale labels it.
        static constexpr int kNumberSpacing = 64;

        // The prominence tier of a frame's tick. This is the Scaler's output:
        // which frames get a tick, and how prominent it is, is a property of
        // the FPS divisor chain and the zoom level. How a tier is *drawn*
        // (its length and alpha) is an appearance concern and lives with the
        // painter — the Scaler only classifies.
        enum class TickLevel { None, Minor, Intermediate, Major };

        struct Attribute {
            TickLevel level = TickLevel::None;
            int pos = 0;          // on-screen x (content-axis) offset of the frame
            bool showNumber = false;
        };

        Scaler();

        void setFrameEnd(int aFrameEnd);
        void setFps(int aFps);
        // Applies a wheel delta; returns false when the zoom is already
        // clamped at the floor or ceiling and the delta had no room to act.
        // (Sub-notch deltas still report true: trackpad steps must each
        // re-anchor the view even when the pixel spacing does not change.)
        bool update(int aWheelDelta);
        // Moves the ladder straight to a rung (clamped). update() is for wheel deltas; this is for
        // a caller that computed the rung it wants (the graph panel's fit and its window mapping),
        // where the delta arithmetic - update() SUBTRACTS its argument - is easy to get backwards.
        void setIndex(int aIndex);
        // The frame axis' rate: kZoomPerNotch ^ index, so one notch is the same relative change at
        // every zoom (see THE LADDER in the source). Fractional, hence a double.
        double pixelsPerFrame() const;
        int index() const { return mIndex; }
        // The index for a rate a caller already has, rounded to the SPARSEST rung that still
        // fits it (rate <= aRate): the graph panel fits a window into its plot with this.
        int indexForRate(double aRate) const;
        int pixelWidth(int aFrame) const;
        int maxPixelWidth() const;
        int frame(int aPixelWidth) const;
        int frameEnd() const;
        Attribute attribute(int aFrame) const;
        // The finest tick level the ladder draws at this spacing: every drawn tick is a multiple
        // of it, so a loop over the visible frames may iterate in strides of it. It exists so a
        // deep zoom-out (whose visible span is viewport / spacing frames) cannot ask such a loop
        // for millions of iterations.
        int tickStride(double aSpacing) const;
        // attribute() at an arbitrary spacing: for a caller whose mapping is not this
        // scaler's rung (the graph panel while a timeline owns its frame axis - the
        // mapping is the exact fit of a pushed window, fractional and outside this
        // scaler's own zoom range). pos is the same content-axis offset, rounded.
        Attribute attributeAt(int aFrame, double aSpacing) const;

    private:
        int majorStep(double aSpacing) const;
        Attribute attributeFor(int aFrame, double aSpacing) const;

        int mFrameEnd;
        int mFps;
        std::vector<int> mLadder;
        int mWheel;
        int mIndex;
    };

} // namespace time
} // namespace ctrl

#endif // CTRL_TIME_SCALER_H
