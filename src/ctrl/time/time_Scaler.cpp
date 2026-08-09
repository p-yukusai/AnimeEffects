#include "ctrl/time/time_Scaler.h"

namespace {
static const int kWheelValue = 120;
static const int kMinScaleRaw = 1 * kWheelValue;
static const int kMaxScaleRaw = 15 * kWheelValue;
} // namespace

namespace ctrl {
namespace time {

    //-------------------------------------------------------------------------------------------------
    Scaler::Scaler(): mMaxFrame(), mWheel(kWheelValue), mIndex(1), mFrameList() {}

    void Scaler::setMaxFrame(int aMaxFrame) { mMaxFrame = aMaxFrame; }

    void Scaler::setFrameList(const std::array<int, 3>& aFrameList) { mFrameList = aFrameList; }

    void Scaler::update(int aWheelDelta) {
        mWheel = std::max(kMinScaleRaw, std::min(kMaxScaleRaw, mWheel - aWheelDelta));
        mIndex = mWheel / kWheelValue;
    }

    int Scaler::pixelWidth(int aFrame) const {
        const int frame = std::max(0, std::min(mMaxFrame, aFrame));
        return (mIndex + 1) * frame;
    }

    int Scaler::maxPixelWidth() const { return pixelWidth(mMaxFrame); }

    int Scaler::frame(int aPixelWidth) const {
        const int frame = (aPixelWidth + ((mIndex + 1) >> 1)) / (mIndex + 1);
        return std::max(0, std::min(mMaxFrame, frame));
    }

    int Scaler::maxFrame() const { return mMaxFrame; }

    Scaler::Attribute Scaler::attribute(int aFrame) const {
        Attribute attr;
        const int spacing = mIndex + 1; // on-screen pixels per frame
        attr.grid.setX(spacing * aFrame);

        if (aFrame % mFrameList[0] == 0) {
            attr.grid.setY(10);
            attr.showNumber = true;
        } else if (aFrame % mFrameList[1] == 0) {
            attr.grid.setY(8);
            // Number a tick tier only once it has room on screen, so zooming in
            // reveals progressively finer frame numbers.
            attr.showNumber = (spacing * mFrameList[1] >= 64);
        } else if (aFrame % mFrameList[2] == 0) {
            attr.grid.setY(6);
            attr.showNumber = (spacing * mFrameList[2] >= 64);
        } else {
            // Drop the per-frame minor ticks when zoomed out far enough that
            // they would pack tighter than a few pixels apart.
            attr.grid.setY(spacing >= 4 ? 3 : 0);
            attr.showNumber = false;
        }
        return attr;
    }

} // namespace time
} // namespace ctrl
