#include "gui/qml/AnimationSettingViewModel.h"
#include <algorithm>

namespace gui {

AnimationSettingViewModel::AnimationSettingViewModel(int aStart, int aEnd, int aFps, bool aLoop, QObject* aParent)
    : QObject(aParent), mFrameStart(aStart), mFrameEnd(aEnd), mFps(aFps), mLoop(aLoop) {
    Q_ASSERT(aStart <= aEnd);
    Q_ASSERT(aFps >= 1);
}

// The QML scene enforces start <= end via cross-field from/to bindings on the
// ScrubSpinBoxes, so a value arriving here is already range-valid. Store it
// directly rather than re-clamp, so the displayed value never disagrees with
// the stored one.
//
// Each setter records ONE session-history entry when the value changes and the
// edit is not part of a continuous gesture (a scrub drag is coalesced by
// beginGesture/endGesture). The whole session still commits to the model as a
// single undo step on close.
void AnimationSettingViewModel::setFrameStart(int aValue) {
    if (aValue == mFrameStart) return;
    const State old = state();
    mFrameStart = aValue;
    if (!mGestureActive) pushState(old);
    emit frameStartChanged();
}

void AnimationSettingViewModel::setFrameEnd(int aValue) {
    if (aValue == mFrameEnd) return;
    const State old = state();
    mFrameEnd = aValue;
    if (!mGestureActive) pushState(old);
    emit frameEndChanged();
}

void AnimationSettingViewModel::setFps(int aValue) {
    aValue = std::max(aValue, 1);
    if (aValue == mFps) return;
    const State old = state();
    mFps = aValue;
    if (!mGestureActive) pushState(old);
    emit fpsChanged();
}

void AnimationSettingViewModel::setLoop(bool aValue) {
    if (aValue == mLoop) return;
    const State old = state();
    mLoop = aValue;
    if (!mGestureActive) pushState(old);
    emit loopChanged();
}

void AnimationSettingViewModel::reset(int aStart, int aEnd, int aFps, bool aLoop) {
    mFrameStart = aStart;
    mFrameEnd = aEnd;
    mFps = aFps;
    mLoop = aLoop;
    mUndo.clear();
    mRedo.clear();
    mGestureStart = State();
    mGestureActive = false;
    emit frameStartChanged();
    emit frameEndChanged();
    emit fpsChanged();
    emit loopChanged();
}

// ---- session undo/redo -----------------------------------------------------
AnimationSettingViewModel::State AnimationSettingViewModel::state() const {
    return State{ mFrameStart, mFrameEnd, mFps, mLoop };
}

void AnimationSettingViewModel::applyState(const State& aState) {
    if (mFrameStart != aState.start) { mFrameStart = aState.start; emit frameStartChanged(); }
    if (mFrameEnd != aState.end) { mFrameEnd = aState.end; emit frameEndChanged(); }
    if (mFps != aState.fps) { mFps = aState.fps; emit fpsChanged(); }
    if (mLoop != aState.loop) { mLoop = aState.loop; emit loopChanged(); }
}

void AnimationSettingViewModel::pushState(const State& aState) {
    mUndo.push_back(aState);
    mRedo.clear();
}

void AnimationSettingViewModel::beginGesture() {
    // Snapshot the pre-drag state so endGesture can fold the whole drag into one
    // history entry. Do not record yet: a press-without-move must not leave one.
    mGestureStart = state();
    mGestureActive = true;
}

void AnimationSettingViewModel::endGesture() {
    if (!mGestureActive) return;
    mGestureActive = false;
    if (state() != mGestureStart)
        pushState(mGestureStart); // one entry per drag
}

void AnimationSettingViewModel::undo() {
    if (mUndo.isEmpty()) return;
    mRedo.push_back(state());
    applyState(mUndo.takeLast());
}

void AnimationSettingViewModel::redo() {
    if (mRedo.isEmpty()) return;
    mUndo.push_back(state());
    applyState(mRedo.takeLast());
}

void AnimationSettingViewModel::reject() {
    emit rejected();
}

} // namespace gui
