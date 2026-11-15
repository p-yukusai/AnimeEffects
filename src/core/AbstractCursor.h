#ifndef CORE_ABSTRACTCURSOR_H
#define CORE_ABSTRACTCURSOR_H

#include <functional>
#include <QMouseEvent>
#include <QPoint>
#include "core/CameraInfo.h"
#include <QTimer>

namespace core {

class AbstractCursor {
public:
    enum Event { Event_Press, Event_Move, Event_Release, Event_TERM };

    enum Button { Button_Left, Button_Middle, Button_Right, Button_TERM };

    AbstractCursor();

    /// update cursor by mouse events.
    /// these functions will return true when the caller should reflect the new cursor event.
    /// @{
    bool setMousePress(QMouseEvent* aEvent, const CameraInfo& aCameraInfo);
    bool setMouseMove(QMouseEvent* aEvent, const CameraInfo& aCameraInfo);
    bool setMouseRelease(QMouseEvent* aEvent, const CameraInfo& aCameraInfo);
    bool setMouseDoubleClick(QMouseEvent* aEvent, const CameraInfo& aCameraInfo);
    /// @}

    /// tablet event
    /// Tablet behavior is difference between mac and windows. It's Qt's bug?
    /// In windows, Duplicate mouse events occur while operating a tablet.
    bool setTabletEvent(QTabletEvent* aEvent, const CameraInfo& aCameraInfo);
    void setTabletPressure(QTabletEvent* aEvent);

    void suspendEvent(const std::function<void()>& aEventReflector);
    void resumeEvent();

    inline Button eventButton() const { return mEventButton; }
    inline Event eventType() const { return mEventType; }
    inline QPoint screenPoint() const { return mScreenPoint; }
    inline QVector2D screenPos() const { return mScreenPos; }
    inline QVector2D screenVel() const { return mScreenVel; }
    inline QPoint worldPoint() const { return mWorldPos.toPoint(); }
    inline QVector2D worldPos() const { return mWorldPos; }
    inline QVector2D worldVel() const { return mWorldVel; }
    inline float pressure() const { return mPressure; }

    inline bool isPressed(Button aButton) const { return mIsPressed.at(aButton); }
    inline bool isPressedLeft() const { return mIsPressed[Button_Left]; }
    inline bool isPressedMiddle() const { return mIsPressed[Button_Middle]; }
    inline bool isPressedRight() const { return mIsPressed[Button_Right]; }

    bool emitsLeftPressedEvent() const;
    bool emitsLeftDraggedEvent() const;
    bool emitsLeftReleasedEvent() const;
    bool emitsRightPressedEvent() const;
    bool emitsRightDraggedEvent() const;
    bool emitsRightReleasedEvent() const;
    bool emitsPressedEvent() const;

    bool shouldDoubleClick = false;
    bool shouldResetCanvas = false;
    bool initializedTimer = false;
    QTimer *doubleClickTimer = nullptr;

private:
    // A POINTER POSITION IS SUB-PIXEL, EVEN THOUGH screenPoint() IS NOT: the event's QPointF is kept
    // as-is in screenPos()/worldPos(), and screenPoint() rounds only for the callers that ask for a
    // whole pixel. Rounding here instead snapped every sample to the device grid before the tools
    // saw it: a stroke drawn at 0.5 zoom landed on a 2-content-pixel grid and looked quantized, and
    // the path smoother's fractional output was thrown away at this line (measured, 2026-10-31).
    bool setMousePressImpl(Qt::MouseButton aButton, QPointF aPos, const CameraInfo& aCameraInfo);
    bool setMouseMoveImpl(Qt::MouseButton aButton, QPointF aPos, const CameraInfo& aCameraInfo);
    bool setMouseReleaseImpl(Qt::MouseButton aButton, QPointF aPos, const CameraInfo& aCameraInfo);

    Event mEventType;
    Button mEventButton;
    std::array<bool, Button_TERM> mIsPressed;
    std::array<bool, Button_TERM> mIsDouble;
    QPoint mScreenPoint;
    QVector2D mScreenPos;
    QVector2D mScreenVel;
    QVector2D mWorldPos;
    QVector2D mWorldVel;
    float mPressure;
    bool mIsPressedTablet;
    int mSuspendedCount;
    std::array<bool, Button_TERM> mBlankAfterSuspending;
};

} // namespace core
#endif // CORE_ABSTRACTCURSOR_H