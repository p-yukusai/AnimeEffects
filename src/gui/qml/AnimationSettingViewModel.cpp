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
void AnimationSettingViewModel::setFrameStart(int aValue) {
    if (aValue == mFrameStart) return;
    mFrameStart = aValue;
    emit frameStartChanged();
}

void AnimationSettingViewModel::setFrameEnd(int aValue) {
    if (aValue == mFrameEnd) return;
    mFrameEnd = aValue;
    emit frameEndChanged();
}

void AnimationSettingViewModel::setFps(int aValue) {
    aValue = std::max(aValue, 1);
    if (aValue == mFps) return;
    mFps = aValue;
    emit fpsChanged();
}

void AnimationSettingViewModel::setLoop(bool aValue) {
    if (aValue == mLoop) return;
    mLoop = aValue;
    emit loopChanged();
}

void AnimationSettingViewModel::reject() {
    emit rejected();
}

} // namespace gui
