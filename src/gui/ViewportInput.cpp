#include "gui/ViewportInput.h"

#include <QGuiApplication>
#include <QLineF>
#include <QMouseEvent>
#include <QTabletEvent>

#include "core/AbstractCursor.h"
#include "core/CameraInfo.h"
#include "core/RenderInfo.h"
#include "ctrl/Brush.h"
#include "ctrl/Driver.h"
#include "ctrl/MaskEditor.h"
#include "ctrl/ToolType.h"

namespace gui {

namespace {

// The tools are told the modifier state through the driver (neither the cursor nor the editor's
// updateCursor carries it), every sample.
inline void forwardModifiers(ViewportHost& aHost, Qt::KeyboardModifiers aModifiers) {
    aHost.forwardModifiers(aModifiers);
}

} // namespace

ViewportInput::ViewportInput(ViewportHost& aHost): mHost(aHost) {}

Qt::MouseButtons ViewportInput::cursorButtons() const {
    Qt::MouseButtons buttons = Qt::NoButton;
    const core::AbstractCursor& cursor = mHost.cursor();
    if (cursor.isPressedLeft())
        buttons |= Qt::LeftButton;
    if (cursor.isPressedMiddle())
        buttons |= Qt::MiddleButton;
    if (cursor.isPressedRight())
        buttons |= Qt::RightButton;
    return buttons;
}

Sampling ViewportInput::policyFor(
    Gesture aGesture, ctrl::ToolType aToolType, const PointerSample& aSample
) {
    // A PEN'S OUTCOME IS ITS PATH, always (pressure and tilt have no "end state")
    if (aSample.precise)
        return Sampling::Every;
    switch (aGesture) {
    case Gesture::Eyedropper:
    case Gesture::BrushResize:
        // a readout and a dial: only the current position matters
        return Sampling::Latest;
    case Gesture::Tool:
        // A PAN IS THE POINTER' S END STATE (the camera only ever follows it), so any pan button is
        // Latest. A BRUSH STROKE IS THE PATH ITSELF: every sample is part of the artwork, so the
        // painting tools are Every - compressing them is exactly the "simplified stroke" defect.
        if ((aSample.buttons & (Qt::MiddleButton | Qt::RightButton)) != Qt::NoButton)
            return Sampling::Latest;
        switch (aToolType) {
        case ctrl::ToolType_FFD:
        case ctrl::ToolType_Bone:
        case ctrl::ToolType_Pose:
        case ctrl::ToolType_Mask:
            return Sampling::Every;
        default:
            return Sampling::Latest;
        }
    case Gesture::None:
        break;
    }
    return Sampling::Latest;
}

bool ViewportInput::press(const PointerSample& aSample) {
    if (mGesture != Gesture::None)
        releaseGesture(aSample);

    // priority: the eyedropper claims Alt+left before any tool, the resize claims Alt+right before
    // any tool, everything else is the tool's
    if ((aSample.button == Qt::LeftButton) && (aSample.modifiers & Qt::AltModifier)
        && mHost.maskEditor()) {
        mGesture = Gesture::Eyedropper;
    } else if ((aSample.button == Qt::RightButton) && (aSample.modifiers & Qt::AltModifier)
               && mHost.activeBrush()) {
        mGesture = Gesture::BrushResize;
    } else {
        mGesture = Gesture::Tool;
    }
    mSampling = policyFor(mGesture, mHost.toolType(), aSample);
    mLast = aSample;
    mPending = aSample;

    // A DRAWING PATH STARTS EXACTLY UNDER THE PEN: the smoother anchors its window at the press.
    // Any other gesture resets it, so no sample ever carries across gestures.
    if (mGesture == Gesture::Tool && smoothingApplies(aSample))
        mSmoothing.begin(aSample.pos);
    else
        mSmoothing.reset();

    switch (mGesture) {
    case Gesture::Eyedropper:
        return pressEyedropper(aSample);
    case Gesture::BrushResize:
        return pressBrushResize(aSample);
    case Gesture::Tool:
        return pressTool(aSample);
    case Gesture::None:
        break;
    }
    return false;
}

bool ViewportInput::pressEyedropper(const PointerSample& aSample) {
    // the press samples immediately, so a click reads even if no frame runs before the release
    sampleEyedropper(aSample);
    return true;
}

bool ViewportInput::pressBrushResize(const PointerSample& aSample) {
    // the dial only starts; nothing fires until the pointer leaves the slop and latches an axis
    mHost.beginBrushDial(aSample.pos);
    return true;
}

bool ViewportInput::pressTool(const PointerSample& aSample) {
    forwardModifiers(mHost, aSample.modifiers);
    bool taken = false;
    if (aSample.tablet) {
        taken = mHost.cursor().setTabletEvent(
            const_cast<QTabletEvent*>(aSample.tablet), mHost.camera()
        );
    } else {
        QMouseEvent event(
            QEvent::MouseButtonPress, aSample.pos, mHost.mapToGlobal(aSample.pos.toPoint()),
            aSample.button, aSample.buttons, aSample.modifiers
        );
        taken = mHost.cursor().setMousePress(&event, mHost.camera());
    }
    if (!taken)
        return false;
    mHost.updateCursorNow();
    // the pan's closed hand
    if (aSample.button == Qt::MiddleButton || aSample.button == Qt::LeftButton)
        mHost.setGrabCursor(true);
    return true;
}

bool ViewportInput::move(const PointerSample& aSample) {
    if (mGesture == Gesture::None) {
        // NO GESTURE, BUT STILL A SAMPLE THE TOOL WANTS: the brush's ring and the tool's own cursor
        // follow the pointer with no button down. Returning false here was the regression that made
        // the ring move only while a button was pressed.
        hoverTool(aSample);
        // the tool's cursor moved: the overlay (a brush ring, a gizmo) has to be redrawn
        mHost.requestFrame();
        return true;
    }
    mLast = aSample;
    mPending = aSample;

    if (mSampling == Sampling::Latest) {
        // a pen never reaches here (policyFor makes every pen sample Every), but the invariant is
        // cheap to keep: a held sample must not point into the caller's event
        mPending.tablet = nullptr;
        mPending.precise = false;
        // HELD, NOT APPLIED: the frame will feed it once (see frameTick). This is what keeps the
        // event loop free while a move/pan/resize is in flight.
        mHost.requestFrame();
        return true;
    }
    // Every: applied on arrival, because the sample IS part of the outcome
    switch (mGesture) {
    case Gesture::Eyedropper:
        sampleEyedropper(aSample);
        break;
    case Gesture::BrushResize:
        sampleBrushResize(aSample);
        break;
    case Gesture::Tool:
        sampleTool(aSample);
        break;
    case Gesture::None:
        break;
    }
    mHost.requestFrame();
    return true;
}

bool ViewportInput::release(const PointerSample& aSample) {
    if (mGesture == Gesture::None)
        return false;
    // THE FINAL POSITION LANDS FIRST, whatever the sampling: the end state must be exactly under
    // the pointer, and a frame may never have run between the last move and this release.
    mPending = aSample;
    if (mSampling == Sampling::Latest)
        frameTick();
    else if (mGesture == Gesture::Tool) {
        sampleTool(aSample);
        // THE MEAN ENDS D/2 px BEHIND THE NIB, and a stroke must end under the pen. The lab's
        // "finished" pane re-evaluates the tail; a stamping editor cannot re-draw what it already
        // put down, so the RAW release is fed last and the tool draws one short segment from the
        // smoothed tip to it (measured: at most lagPx(), 6 px at the default window).
        if (smoothingApplies(aSample)
            && QLineF(mSmoothing.value(), aSample.pos).length() > 0.25) {
            feedTool(aSample);
        }
    } else if (mGesture == Gesture::Eyedropper)
        sampleEyedropper(aSample);

    releaseGesture(aSample);
    mHost.requestFrame();
    return true;
}

void ViewportInput::releaseGesture(const PointerSample& aSample) {
    const Gesture gesture = mGesture;
    mGesture = Gesture::None;
    mPending.pos = QPointF(-1.0, -1.0);
    // the path's smoother never carries a sample into the next gesture
    mSmoothing.reset();

    if (gesture == Gesture::BrushResize) {
        mHost.endBrushDial();
        return;
    }
    if (gesture != Gesture::Tool)
        return;
    forwardModifiers(mHost, aSample.modifiers);
    bool taken = false;
    if (aSample.tablet) {
        taken = mHost.cursor().setTabletEvent(
            const_cast<QTabletEvent*>(aSample.tablet), mHost.camera()
        );
    } else {
        QMouseEvent event(
            QEvent::MouseButtonRelease, aSample.pos, mHost.mapToGlobal(aSample.pos.toPoint()),
            aSample.button, aSample.buttons, aSample.modifiers
        );
        taken = mHost.cursor().setMouseRelease(&event, mHost.camera());
    }
    if (taken)
        mHost.updateCursorNow();
    mHost.setGrabCursor(false);
}

bool ViewportInput::frameTick() {
    if (mGesture == Gesture::None || mSampling != Sampling::Latest)
        return false;
    if (mPending.pos == QPointF(-1.0, -1.0))
        return false;
    switch (mGesture) {
    case Gesture::Eyedropper:
        sampleEyedropper(mPending);
        break;
    case Gesture::BrushResize:
        sampleBrushResize(mPending);
        break;
    case Gesture::Tool:
        feedTool(mPending);
        break;
    case Gesture::None:
        return false;
    }
    return true;
}

void ViewportInput::sampleEyedropper(const PointerSample& aSample) {
    ctrl::MaskEditor* editor = mHost.maskEditor();
    if (!editor)
        return;
    const QVector2D world = mHost.camera().toWorldPos(
        QVector2D(float(aSample.pos.x()), float(aSample.pos.y()))
    );
    const float sampled = editor->sampleValue(world);
    if (0.0f <= sampled)
        mHost.sampleMaskValue(sampled);
}

void ViewportInput::sampleBrushResize(const PointerSample& aSample) {
    // the dial's steps are the BrushDrag's business (ctrl/Brush.h); the host applies them
    mHost.stepBrushDial(aSample.pos);
}

void ViewportInput::sampleTool(const PointerSample& aSample) { feedTool(smoothedSample(aSample)); }

bool ViewportInput::smoothingApplies(const PointerSample& aSample) const {
    if (mGesture != Gesture::Tool || mSampling != Sampling::Every)
        return false;                       // only a drawing path; a pan's end state is not a path
    if (!mSmoothing.isEnabled())
        return false;
    if (aSample.tablet)
        return false;                       // a pen's ink stays under the nib (see the header)
    // NO BUTTON TEST: a drag sample often carries an empty `buttons` (the press may have been
    // consumed elsewhere), and the gesture itself is the proof - a hover has no gesture at all
    // (see hoverTool), so it never reaches this seam.
    if ((aSample.buttons & (Qt::MiddleButton | Qt::RightButton)) != Qt::NoButton)
        return false;                       // a pan that happens to be an Every tool
    return true;
}

PointerSample ViewportInput::smoothedSample(const PointerSample& aSample) {
    if (!smoothingApplies(aSample))
        return aSample;
    PointerSample out = aSample;
    out.pos = mSmoothing.step(aSample.pos);
    return out;
}

void ViewportInput::hoverTool(const PointerSample& aSample) {
    // the same call the drag makes, with no button held: `AbstractCursor` derives "dragged" from its
    // own pressed state, so a button-less sample can only ever be a hover
    PointerSample hover = aSample;
    hover.button = Qt::NoButton;
    hover.buttons = Qt::NoButton;
    hover.tablet = nullptr;
    hover.precise = false;
    feedTool(hover);
}

void ViewportInput::feedTool(const PointerSample& aSample) {
    forwardModifiers(mHost, aSample.modifiers);
    if (aSample.tablet) {
        if (mHost.cursor().setTabletEvent(
                const_cast<QTabletEvent*>(aSample.tablet), mHost.camera()
            ))
            mHost.updateCursorNow();
        return;
    }
    QMouseEvent event(
        QEvent::MouseMove, aSample.pos, mHost.mapToGlobal(aSample.pos.toPoint()), Qt::NoButton,
        // THE BUTTONS COME FROM THE CURSOR, NOT FROM THE EVENT: a QMouseEvent's `buttons()` is only
        // populated on the widget Qt routed the press to, and the tools derive "dragged" from the
        // cursor's own pressed state anyway. Taking them from the event is what made a stroke a
        // single tap when the press had been consumed elsewhere.
        aSample.buttons, aSample.modifiers
    );
    if (mHost.cursor().setMouseMove(&event, mHost.camera()))
        mHost.updateCursorNow();
}



} // namespace gui
