#ifndef GUI_TIMERULER_H
#define GUI_TIMERULER_H

#include <QWidget>
#include <QString>
#include "core/AbstractCursor.h"
#include "core/CameraInfo.h"
#include "gui/theme/TimeLine.h"
#include "util/Range.h"

namespace ctrl {
class TimeLineEditor;
}

namespace gui {

class TimeLineEditorWidget;

// The ruler: the fixed strip above the track lanes. It is a real widget with
// its own paint (background, ticks, numbers, boundary labels, the temporary
// playback window's band/stems/pills) and its own input (header seek, the
// shift-drag window gesture), fed through the editor's cursor machinery —
// exactly like the track surface — so the controller stays the single
// gesture state machine. The playhead's line and frame-number pill
// (TimeCursor) overlay it; the hairline under it is drawn by the surface
// (it sits on the first track row, outside this widget).
class TimeRuler: public QWidget {
    Q_OBJECT

public:
    TimeRuler(TimeLineEditorWidget& aSurface, QWidget* aParent);

    void setCamera(const core::CameraInfo* aCamera);
    void updateTheme();
    // The MoveCanvas key (space by default) is held; the timeline pans on
    // left-drag, so the strip must let left-presses propagate instead of
    // seeking. Mirrors TimeLineWidget::mPanByKey.
    void setPanKeyHeld(bool aHeld) { mPanKeyHeld = aHeld; }

signals:
    // A ruler gesture moved the current frame (header seek / playhead drag);
    // the surface forwards it to the timeline's onFrameUpdated signal.
    void frameChanged();

protected:
    virtual void paintEvent(QPaintEvent* aEvent) override;
    virtual void mousePressEvent(QMouseEvent* aEvent) override;
    virtual void mouseMoveEvent(QMouseEvent* aEvent) override;
    virtual void mouseReleaseEvent(QMouseEvent* aEvent) override;

private:
    void renderHeader(QPainter& aPainter, const QRect& aCameraRect, const util::Range& aVisibleRange, int aFps);
    void renderPlayWindow(QPainter& aPainter, const QRect& aCameraRect, const util::Range& aVisibleRange, int aFps);
    static void drawPillText(QPainter& aPainter, const QRect& aPill, const QString& aText, const QColor& aColor, int aTop);

    TimeLineEditorWidget& mSurface;
    ctrl::TimeLineEditor* mEditor;
    const core::CameraInfo* mCamera;
    core::AbstractCursor mAbstractCursor;
    theme::TimeLine mTimelineTheme;
    bool mGesturing;   // a left-button gesture holds the mouse grab
    bool mPanKeyHeld;  // the MoveCanvas key is held (see setPanKeyHeld)
};

} // namespace gui

#endif // GUI_TIMERULER_H
