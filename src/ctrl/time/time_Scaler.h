#ifndef CTRL_TIME_SCALER_H
#define CTRL_TIME_SCALER_H

#include <array>
#include <vector>
#include <QPoint>

namespace ctrl {
namespace time {

    class Scaler {
    public:
        // Minimum on-screen distance (px) between consecutive numbers of a
        // tier before the scale labels it.
        static constexpr int kNumberSpacing = 64;

        struct Attribute {
            bool showNumber;
            QPoint grid;
        };

        Scaler();

        void setMaxFrame(int aMaxFrame);
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
        int maxFrame() const;
        Attribute attribute(int aFrame) const;

    private:
        int majorStep(int aSpacing) const;

        int mMaxFrame;
        int mFps;
        std::vector<int> mLadder;
        int mWheel;
        int mIndex;
    };

} // namespace time
} // namespace ctrl

#endif // CTRL_TIME_SCALER_H
