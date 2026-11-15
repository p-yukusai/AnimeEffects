#include "ctrl/time/time_Current.h"
#include "ctrl/time/time_Scaler.h"

namespace ctrl {
namespace time {

    //-------------------------------------------------------------------------------------------------
    Current::Current(int aLeftMargin): mLeftMargin(aLeftMargin), mFrame(0), mPos() { mPos.setY(11); }

    void Current::setFrame(const Scaler& aScale, core::Frame aFrame) {
        mFrame = aFrame;
        mPos.setX(mLeftMargin + aScale.pixelWidth(mFrame.get()));
    }

    void Current::setHandlePos(const Scaler& aScale, const QPoint& aPos) {
        mFrame.set(aScale.frame(aPos.x() - mLeftMargin));
        mPos.setX(mLeftMargin + aScale.pixelWidth(mFrame.get()));
    }

    void Current::update(const Scaler& aScale) { mPos.setX(mLeftMargin + aScale.pixelWidth(mFrame.get())); }

} // namespace time
} // namespace ctrl
