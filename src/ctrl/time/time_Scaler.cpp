#include "ctrl/time/time_Scaler.h"

namespace {
static const int kWheelValue = 120;
static const int kMinScaleRaw = 1 * kWheelValue;
static const int kMaxScaleRaw = 15 * kWheelValue;

// Least prime factor of n; n itself when prime. Trial division is fine: the
// chain below only ever divides a value by its least prime factor, so n
// shrinks fast and sqrt(n) is at most ~46341 for int.
int leastPrimeFactor(int n) {
    for (int d = 2; d * d <= n; ++d) {
        if (n % d == 0)
            return d;
    }
    return n;
}
} // namespace

namespace ctrl {
namespace time {

    //-------------------------------------------------------------------------------------------------
    Scaler::Scaler(): mMaxFrame(), mFps(60), mLadder(), mWheel(kWheelValue), mIndex(1) {
        setFps(mFps);
    }

    void Scaler::setMaxFrame(int aMaxFrame) { mMaxFrame = aMaxFrame; }

    void Scaler::setFps(int aFps) {
        mFps = std::max(1, aFps);

        // The major step ladder: the FPS divisor chain — divide by the least
        // prime factor at each step, which always reaches 1 (24 fps -> 24, 12,
        // 6, 3; 30 fps -> 30, 15, 5; 49 fps -> 49, 7) — plus fps multiples
        // for zoom-out. Every rung divides the next, so any subset stays
        // aligned.
        mLadder.clear();
        int v = mFps;
        mLadder.push_back(v);
        while (v > 1) {
            v /= leastPrimeFactor(v);
            mLadder.push_back(v);
        }
        if (!mLadder.empty() && mLadder.back() == 1)
            mLadder.pop_back(); // 1 is the per-frame minor tier, never a major
        for (int multiple : {2, 3, 4, 5, 6, 10, 20, 50})
            mLadder.push_back(mFps * multiple);
    }

    void Scaler::update(int aWheelDelta) {
        mWheel = std::max(kMinScaleRaw, std::min(kMaxScaleRaw, mWheel - aWheelDelta));
        mIndex = mWheel / kWheelValue;
    }

    int Scaler::majorStep(int aSpacing) const {
        // The major tick grid follows the project FPS and its divisor chain
        // (30 fps -> 0, 30, 60, ...; zoomed in -> 0, 15, 30, ...), but the
        // density is set by the zoom level. Pick the ladder step whose
        // on-screen spacing is closest to a comfortable target
        // (kTargetMajorPx) rather than the densest one above the floor: a
        // cap-and-pack model pushes every rung down to the minimum spacing,
        // which reads as "too close". The hard floor still guarantees
        // legibility. Because the ladder is FPS-derived, at the same zoom a
        // 60 fps project shows the same tick density as a 30 fps one instead
        // of half the ticks for no reason.
        constexpr int kTargetMajorPx = 96;
        constexpr int kMinMajorPx = 48;
        int best = 0;
        qint64 bestDiff = -1;
        for (int step : mLadder) {
            const qint64 spacing = static_cast<qint64>(aSpacing) * step;
            if (spacing < kMinMajorPx)
                continue;
            const qint64 diff = qAbs(spacing - kTargetMajorPx);
            // Ties prefer the sparser step.
            if (bestDiff < 0 || diff < bestDiff ||
                (diff == bestDiff && spacing > static_cast<qint64>(aSpacing) * best)) {
                best = step;
                bestDiff = diff;
            }
        }
        // mLadder always contains a rung at or above kMinMajorPx (fps*50),
        // so best is never 0.
        return best;
    }

    int Scaler::pixelWidth(int aFrame) const {
        const int frame = std::max(0, std::min(mMaxFrame, aFrame));
        return (mIndex + 1) * frame;
    }

    int Scaler::maxPixelWidth() const { return pixelWidth(mMaxFrame); }

    int Scaler::frame(int aPixelWidth) const {
        const int frame = (aPixelWidth + ((mIndex + 1) >> 1)) / (mIndex + 1);
        return std::max(0, std::min(mMaxFrame, frame));
    }

    int Scaler::maxFrame() const { return mMaxFrame; }

    Scaler::Attribute Scaler::attribute(int aFrame) const {
        Attribute attr;
        const int spacing = mIndex + 1; // on-screen pixels per frame
        attr.grid.setX(spacing * aFrame);

        // One intermediate level: the largest proper divisor of the major
        // step (step / least prime factor) — the rung directly below the
        // major in the divisor chain: 24 -> 12, 30 -> 15, 15 -> 5, 49 -> 7.
        // It sits at roughly half the major's on-screen spacing (majors stay
        // at 72-128px, so the rung lands 36-64px, near the 48px target), it
        // divides the major step by construction so ticks always align (a
        // truncated half, 15 -> 7, would misalign with 0, 7, 14, 15), and an
        // even major beyond 128px cannot occur — its own half-rung would be a
        // closer major candidate. Prime steps have no intermediate; the
        // per-frame tier below is the only other minor level (two minors max).
        const int step = majorStep(spacing);
        const int tier = step / leastPrimeFactor(step);

        if (aFrame % step == 0) {
            attr.grid.setY(10);
            attr.showNumber = true;
        } else if (tier > 1 && aFrame % tier == 0 &&
                   static_cast<qint64>(spacing) * tier >= 16) {
            attr.grid.setY(8);
            // Same room-to-read rule as the per-frame tier: an 8px tick needs
            // >= 2x its height of gap, or it smears into a band (square-ish
            // FPS like 121 -> 11-frame rung would otherwise be unreadable).
            // Number a tick tier only once it has room on screen, so zooming in
            // reveals progressively finer frame numbers.
            attr.showNumber = (static_cast<qint64>(spacing) * tier >= kNumberSpacing);
        } else {
            // Per-frame minor ticks only once frames have room to read as
            // distinct lines (gap >= tick height); any closer and they smear
            // into a solid band.
            attr.grid.setY(spacing >= 6 ? 3 : 0);
            attr.showNumber = false;
        }
        return attr;
    }

} // namespace time
} // namespace ctrl
