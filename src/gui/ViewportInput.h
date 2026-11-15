#ifndef GUI_VIEWPORTINPUT_H
#define GUI_VIEWPORTINPUT_H

#include <QPointF>
#include <QRect>
#include <QVector2D>
#include <Qt>

#include "gui/PointerSmoothing.h"

class QTabletEvent;
#include "ctrl/ToolType.h"

namespace core {
class RenderInfo;
class CameraInfo;
class AbstractCursor;
} // namespace core

namespace ctrl {
class MaskEditor;
struct BrushParam;
} // namespace ctrl

namespace gui {

// ONE POINTER SAMPLE. The viewport handlers build this and hand it to ViewportInput; nothing else
// in the widget routes pointer input.
struct PointerSample {
    QPointF pos;
    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButtons buttons = Qt::NoButton;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    // A PEN IS ALWAYS `Every`, AND IT IS NEVER HELD: its pressure and tilt live only in the event, so
    // the router applies it in the call that carried it. That is what makes holding a pointer to the
    // caller's event safe - the invariant is enforced where the sample is built (see
    // MainDisplayWidget::tabletEvent) and checked in ViewportInput::move.
    const QTabletEvent* tablet = nullptr;
    bool precise = false;
};

// What a gesture needs from the pointer. THE declaration that decides whether a sample can be
// dropped or must be applied, so no handler has to guess:
//
//  - Latest: only where the pointer IS matters - the in-between states are not part of the end
//    state. Move/rotate/scale, the canvas pan and the brush resize are all this. They are fed ONCE
//    PER FRAME, which is both cheaper and the reason the event loop stays free enough to deliver.
//  - Every: each sample changes the outcome - a brush stroke is the canvas path itself. These are
//    fed on arrival, and never compressed.
enum class Sampling { Latest, Every };

// The viewport services a gesture needs. MainDisplayWidget implements it; the gestures below never
// touch a widget, a QMouseEvent or a timer.
class ViewportHost {
public:
    virtual ~ViewportHost() = default;
    virtual core::RenderInfo& renderInfo() = 0;
    virtual core::CameraInfo& camera() = 0;
    virtual core::AbstractCursor& cursor() = 0;
    // the editor the eyedropper reads, or null: the router never needs a whole Driver, and a narrow
    // seam is what lets tests/viewport_input.cpp drive this class without one
    virtual ctrl::MaskEditor* maskEditor() = 0;
    // the tools read the modifier state through the driver
    virtual void forwardModifiers(Qt::KeyboardModifiers aModifiers) = 0;
    virtual ctrl::ToolType toolType() const = 0;
    // the ACTIVE brush (null when the tool has none): the resize dial and the preview read it
    virtual const ctrl::BrushParam* activeBrush() const = 0;
    // one call into the tool/editor pipeline, exactly as a handler used to make it
    virtual void updateCursorNow() = 0;
    // one repaint request, coalesced to the frame rate (never a synchronous paint)
    virtual void requestFrame() = 0;
    // the eyedropper's own panel write
    virtual void sampleMaskValue(float aValue) = 0;
    // the brush size/hardness dial: begin at the press, then one step per frame
    virtual void beginBrushDial(const QPointF& aPos) = 0;
    virtual void stepBrushDial(const QPointF& aPos) = 0;
    virtual bool brushDialActive() const = 0;
    virtual void endBrushDial() = 0;
    virtual QPoint mapToGlobal(const QPoint& aPos) = 0;
    // the canvas grab's closed hand
    virtual void setGrabCursor(bool aClosed) = 0;
};

// THE VIEWPORT'S ONE INPUT ROUTER.
//
// One gesture owns the pointer at a time. It declares its Sampling; the router then either hands it
// every sample (Every) or records the latest position and hands it one sample per frame (Latest),
// with the release always landing the final position first. A frame's duties are frameTick():
// feed a Latest gesture, then nothing else - blending and drawing are the caller's.
class ViewportInput {
public:
    explicit ViewportInput(ViewportHost& aHost);

    // The handlers' whole job: build a PointerSample and call these.
    bool press(const PointerSample& aSample);
    bool move(const PointerSample& aSample);
    bool release(const PointerSample& aSample);

    // The frame's duty: feed a Latest gesture the position it has been holding. Returns true when a
    // sample was applied.
    bool frameTick();


    Sampling sampling() const { return mSampling; }
    bool isBusy() const { return mGesture != Gesture::None; }

    // the brush's path smoother (see PointerSmoothing): the host may tune or disable it
    PointerSmoothing& smoothing() { return mSmoothing; }
    const PointerSmoothing& smoothing() const { return mSmoothing; }

private:
    enum class Gesture { None, Eyedropper, BrushResize, Tool };

    // the ONE place that answers "may this sample be dropped?"
    static Sampling policyFor(Gesture aGesture, ctrl::ToolType aToolType, const PointerSample& aSample);

    // Which samples the smoother may touch: a DRAWING PATH, and only a mouse. A pen is excluded
    // because its ink has to stay under the nib (a stylus reports sub-pixel motion at its own rate),
    // a pan because its end state is the only thing that matters, and a hover because the brush's
    // ring must follow the pointer exactly.
    bool smoothingApplies(const PointerSample& aSample) const;
    // The sample the tool is actually fed: the smoothed position when the seam applies.
    PointerSample smoothedSample(const PointerSample& aSample);

    bool pressEyedropper(const PointerSample& aSample);
    bool pressBrushResize(const PointerSample& aSample);
    bool pressTool(const PointerSample& aSample);

    void sampleEyedropper(const PointerSample& aSample);
    void sampleBrushResize(const PointerSample& aSample);
    void sampleTool(const PointerSample& aSample);

    void releaseGesture(const PointerSample& aSample);

    // the tool path's one call: the cursor's own previous position is what the tools derive their
    // delta from, so this is exact whatever was dropped between two calls
    void feedTool(const PointerSample& aSample);
    // a move with NO gesture: the tool still needs it (the brush ring follows the pointer)
    void hoverTool(const PointerSample& aSample);
    // the cursor's own pressed buttons (see the comment in the .cpp)
    Qt::MouseButtons cursorButtons() const;

    ViewportHost& mHost;
    Gesture mGesture = Gesture::None;
    Sampling mSampling = Sampling::Latest;
    // the brush's path smoother, in screen pixels (see PointerSmoothing)
    PointerSmoothing mSmoothing;
    // the Latest gesture's held position; a QPointF(-1,-1) means "nothing held"
    PointerSample mPending;
    PointerSample mLast;
};

} // namespace gui

#endif // GUI_VIEWPORTINPUT_H
