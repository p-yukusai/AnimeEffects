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
        int pixelsPerFrame() const { return mIndex + 1; }
        int pixelWidth(int aFrame) const;
        int maxPixelWidth() const;
        int frame(int aPixelWidth) const;
        int frameEnd() const;
        Attribute attribute(int aFrame) const;

    private:
        int majorStep(int aSpacing) const;

        int mFrameEnd;
        int mFps;
        std::vector<int> mLadder;
        int mWheel;
        int mIndex;
    };

} // namespace time
} // namespace ctrl

#endif // CTRL_TIME_SCALER_H
