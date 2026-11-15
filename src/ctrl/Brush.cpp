#include "ctrl/Brush.h"

namespace ctrl {

void BrushStroke::begin(const BrushParam& aParam, const QVector2D& aPos, float aPressure, Sink aSink) {
    mParam = aParam;
    mSink = std::move(aSink);
    mLastPos = aPos;
    mLastStampPos = aPos;
    mLastPressure = aPressure;
    mNextStamp = 0.0f; // the press point stamps immediately
    mActive = true;
    mStampCount = 0;
    stampAt(aPos, aPressure);
    mNextStamp = mParam.stampStep();
}

void BrushStroke::move(const QVector2D& aPos, float aPressure) {
    if (!mActive || !mSink)
        return;

    const QVector2D segment = aPos - mLastPos;
    const float length = segment.length();
    if (length <= 1.0e-6f)
        return; // no travel: the pressure alone does not stamp

    const QVector2D direction = segment / length;
    const float startPressure = mLastPressure;
    float next = mNextStamp;

    // Walk every stamp the segment covers. The step is recomputed per stamp from
    // the radius the pressure gives it, so a pressure-sized brush tightens its
    // spacing where the pen presses harder instead of leaving a gap.
    int guard = 0;
    const int kMaxStampsPerMove = 4096;
    while (next <= length && guard < kMaxStampsPerMove) {
        const float t = next / length;
        const float pressure = startPressure + (aPressure - startPressure) * t;
        const QVector2D pos = mLastPos + direction * next;
        stampAt(pos, pressure);
        mNextStamp = next + mParam.stampStep() * ((mParam.pressureSize) ? std::max(pressure, 0.02f) : 1.0f);
        next = mNextStamp;
        ++guard;
    }
    // distance carried into the next segment
    mNextStamp = std::max(mNextStamp - length, 0.0f);
    mLastPos = aPos;
    mLastPressure = aPressure;
}

void BrushStroke::end() {
    mActive = false;
    mSink = nullptr;
    mNextStamp = 0.0f;
}

void BrushStroke::stampAt(const QVector2D& aPos, float aPressure) {
    const float pressure = std::min(std::max(aPressure, 0.0f), 1.0f);
    BrushStamp stamp;
    stamp.center = aPos;
    stamp.delta = aPos - mLastStampPos;
    mLastStampPos = aPos;
    stamp.radius = mParam.radius * (mParam.pressureSize ? std::max(pressure, 0.02f) : 1.0f);
    stamp.hardness = mParam.hardness;
    stamp.amount = mParam.flow * (mParam.pressureFlow ? pressure : 1.0f);
    stamp.erase = mParam.erase;
    ++mStampCount;
    mSink(stamp);
}

} // namespace ctrl
