#ifndef CTRL_TIME_MARQUEE_H
#define CTRL_TIME_MARQUEE_H

#include <QPoint>
#include <QRect>
#include <QVector>
#include "core/TimeKeyPos.h"
#include "core/TimeLineEvent.h"
#include "util/Range.h"
#include "ctrl/TimeLineRow.h"
#include "ctrl/time/time_Scaler.h"

namespace ctrl {
namespace time {

    // Result of a single-key hit test.
    struct Hit {
        Hit(): node(), pos() {}
        bool isValid() const { return node && !pos.isNull(); }

        core::ObjectNode* node;
        core::TimeKeyPos pos;
    };

    // The marquee gesture: a pixel-space selection box plus the helpers to
    // hit-test a single key and gather every key inside the box. It owns no
    // selection state of its own — the editor's Selection decides what the
    // gathered keys do. The box tracks the cursor in pixels (no frame snap);
    // frames are only derived at gather time.
    class Marquee {
    public:
        Marquee(const QVector<TimeLineRow>& aRows, const Scaler& aScale, int aMargin);

        // Nearest key within a fixed screen-space radius (px); invalid Hit if
        // the click is not over any key.
        Hit hitTest(const QPoint &aPoint, float scaleFactor) const;

        void begin(const QPoint& aPoint);   // anchor the box
        void update(const QPoint& aPoint);  // resize while dragging
        void clear();

        bool isActive() const { return mActive; }
        QRect rect() const { return QRect(mAnchor, mCurrent).normalized(); }
        util::Range frameRange() const;

        // Every key whose lane and frame fall inside the box.
        void gather(core::TimeLineEvent& aEvent) const;

    private:
        const QVector<TimeLineRow>& mRows;
        const Scaler& mScale;
        int mMargin;
        QPoint mAnchor;
        QPoint mCurrent;
        bool mActive;
    };

} // namespace time
} // namespace ctrl

#endif // CTRL_TIME_MARQUEE_H
