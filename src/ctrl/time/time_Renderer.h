#ifndef CTRL_TIME_RENDERER_H
#define CTRL_TIME_RENDERER_H

#include <QVector>
#include <QPainter>
#include <QSettings>
#include "util/Range.h"
#include "core/CameraInfo.h"
#include "core/ObjectNode.h"
#include "core/TimeFormat.h"
#include "ctrl/TimeLineEditor.h"
#include "ctrl/TimeLineRow.h"
#include "ctrl/time/time_Scaler.h"
#include "gui/theme/TimeLine.h"

namespace ctrl {
    constexpr float radiiFactor = 1.25;
    constexpr float childRadiiFactor = 1.0;
    constexpr double keyRadius = 3.0 * radiiFactor;
    constexpr double keyRadiusSlim = 2.0 * radiiFactor;
    constexpr double keyRadiusChild = 3.0 * childRadiiFactor;
    constexpr double keyRadiusChildPoly = 5.0 * childRadiiFactor;
namespace time {

    class Renderer {
    public:
        Renderer(QPainter& aPainter, const core::CameraInfo& aCamera, const theme::TimeLine& aTheme);

        void setMargin(int aMargin) { mMargin = aMargin; }
        void setRange(const util::Range& aRange) { mRange = aRange; }
        void setTimeScale(const Scaler& aScale) { mScale = &aScale; }
        // The project frame range: the range markers sit at its first and
        // last frame's ticks. The temporary window dims the markers while it
        // overrides; it does not move them.
        void setProjectRange(const util::Range& aRange) { mProjectRange = aRange; }
        // The temporary playback window (see ctrl::TimeLineEditor::PlayWindow);
        // the range markers dim while it is active.
        void setPlayWindow(const ctrl::TimeLineEditor::PlayWindow& aWindow) { mPlayWindow = aWindow; }

        // The track background: fills, lane separators, type labels and the
        // row seams. Keys are a separate pass (renderKeys) so background
        // layers like the range markers can paint between them.
        void renderLines(const QVector<TimeLineRow>& aRows, const QRect& aCameraRect, const QRect& aCullRect);
        void renderRangeMarkers(const QRect& aCameraRect);
        // Key graphics for every row: child-key summaries on closed folders
        // and the per-lane key shapes.
        void renderKeys(const QVector<TimeLineRow>& aRows, const QRect& aCullRect);
        void renderHandle(const QPoint& aPoint, int aRange);
        void renderSelectionRange(const QRect& aRect);

    private:
        void drawTimeCurrent();
        void drawKeys(const core::ObjectNode* aNode, const TimeLineRow& aRow);
        void drawChildKeys(const core::ObjectNode* aNode, const QPoint& aPos);

        QPainter& mPainter;
        const core::CameraInfo& mCamera;
        const theme::TimeLine& mTheme;
        int mMargin;
        util::Range mRange;        // the visible frame range (culling)
        util::Range mProjectRange; // the project frame range (range markers)
        const Scaler* mScale;
        ctrl::TimeLineEditor::PlayWindow mPlayWindow;
    };

} // namespace time
} // namespace ctrl

#endif // CTRL_TIME_RENDERER_H
