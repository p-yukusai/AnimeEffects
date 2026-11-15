#ifndef CTRL_TIME_CURRENT_H
#define CTRL_TIME_CURRENT_H

#include <QPoint>
#include "core/Frame.h"

namespace ctrl {
namespace time {

    class Scaler;

    // The playhead. Like the scale and the keys, it is not clamped to any
    // range — it may sit on any frame (negative, past the project end), so
    // navigation (scrub, header seek, keyboard stepping, transport) is free
    // and never fights the temporary playback window. The window only
    // constrains the playback clock (TimeLineWidget::onPlayBackUpdated),
    // which wraps and stops against the effective range on its own.
    class Current {
    public:
        Current(int aLeftMargin);
        void setFrame(const Scaler& aScale, core::Frame aFrame);
        void setHandlePos(const Scaler& aScale, const QPoint& aPos);
        void update(const Scaler& aScale);
        core::Frame frame() const { return mFrame; }
        QPoint handlePos() const { return mPos; }
        int handleRange() const { return 5; }

    private:
        const int mLeftMargin;
        core::Frame mFrame;
        QPoint mPos;
    };

} // namespace time
} // namespace ctrl

#endif // CTRL_TIME_CURRENT_H
