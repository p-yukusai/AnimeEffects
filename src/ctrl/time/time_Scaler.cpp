#include "ctrl/time/time_Scaler.h"

#include <QtGlobal> // qint64, qAbs
#include <algorithm>
#include <cmath>

namespace {
static const int kWheelValue = 120;
// THE LADDER. One notch multiplies the frame axis' rate by kZoomPerNotch - the application's own
// zoom step, the one the canvas mover and the graph panel's value axis take - so the rate is
// EXPONENTIAL in the index and every notch is the same relative change whatever the current zoom is.
// The index bounds are the only limits, and they are far past any use.
static constexpr double kZoomPerNotch = 1.1;
static constexpr int kMinScaleIndex = -120; // 1.1^-120 = 1.07e-5 px/frame
static constexpr int kMaxScaleIndex = 120;  // 1.1^120  = 93000 px/frame
// The raw wheel accumulator clamps in notch units: the wheel position of an index is
// index * kWheelValue (index 0's notch is 0).
static const int kMinScaleRaw = kMinScaleIndex * kWheelValue;
static const int kMaxScaleRaw = kMaxScaleIndex * kWheelValue;

// The legibility floor for a MAJOR tick, in on-screen pixels: a ladder step whose spacing is below
// this is not drawn at all (the same rule the per-frame minor tier uses). It is reachable only at a
// deep zoom-out - the sparsest rung the ladder has is fps * 50 frames - where the whole content is
// a smudge anyway, and a band of touching marks would be mistaken for content rather than an axis.
static constexpr int kMinMajorPx = 48;
// A ceiling on majorStep()'s doubling below (see there): 1e9 frames is absurd, but the loop needs a
// terminator that does not depend on the spacing.
static constexpr int kMaxMajorStep = 1000000000;

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
    Scaler::Scaler(): mFrameEnd(), mFps(60), mLadder(), mWheel(kWheelValue), mIndex(1) {
        setFps(mFps);
    }

    // The last frame of the project frame range; the scale maps every frame
    // linearly (keys may live anywhere), the end only sizes the content.
    void Scaler::setFrameEnd(int aFrameEnd) { mFrameEnd = aFrameEnd; }

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

    double Scaler::pixelsPerFrame() const { return std::pow(kZoomPerNotch, mIndex); }

    int Scaler::indexForRate(double aRate) const {
        if (!(aRate > 0.0))
            return kMinScaleIndex;
        // The inversion of the ladder above, floored so the rung that comes back is the sparsest one
        // that still fits aRate (rate <= aRate).
        const double index = std::log(aRate) / std::log(kZoomPerNotch);
        return std::max(kMinScaleIndex, std::min(kMaxScaleIndex, static_cast<int>(std::floor(index + 1e-9))));
    }

    int Scaler::tickStride(double aSpacing) const {
        // The finest level attributeFor() draws at this spacing: every drawn tick is a multiple of
        // it, so a loop may iterate frames in strides of it instead of one by one. Without this a
        // deep zoom-out asks such a loop for millions of frames (THE LADDER has no wall now, so
        // the visible span is viewport / spacing and spacing can be tiny). The three levels match
        // attributeFor's three branches, and tests/time_scaler.cpp pins that they agree.
        const int step = majorStep(aSpacing);
        const int tier = step / leastPrimeFactor(step);
        if (aSpacing >= 6.0)
            return 1; // minor ticks: every frame is drawn
        if (tier > 1 && aSpacing * tier >= 16.0)
            return tier; // intermediate ticks
        return step; // majors only
    }

    bool Scaler::update(int aWheelDelta) {
        const int previous = mWheel;
        mWheel = std::max(kMinScaleRaw, std::min(kMaxScaleRaw, mWheel - aWheelDelta));
        mIndex = mWheel / kWheelValue;
        return mWheel != previous;
    }

    void Scaler::setIndex(int aIndex) {
        mIndex = std::max(kMinScaleIndex, std::min(kMaxScaleIndex, aIndex));
        mWheel = mIndex * kWheelValue;
    }

    int Scaler::majorStep(double aSpacing) const {
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
        int best = 0;
        double bestDiff = -1;
        for (int step : mLadder) {
            const double spacing = aSpacing * step;
            if (spacing < kMinMajorPx)
                continue;
            const double diff = std::fabs(spacing - kTargetMajorPx);
            // Ties prefer the sparser step.
            if (bestDiff < 0 || diff < bestDiff || (diff == bestDiff && spacing > aSpacing * best)) {
                best = step;
                bestDiff = diff;
            }
        }
        // No rung reached the floor: even the coarsest step (fps * 50 frames) is too dense to read
        // at a small rate - at 1e-3 px/frame 1200 frames is 1.2px. Doubling from there is the same
        // idea continued, so the ruler stays legible and labels ever wider spans. 0 is never
        // returned: it would be a modulus by zero for attribute() and tickStride(), which both
        // divide by the step.
        int step = best > 0 ? best : mLadder.back();
        while (step < kMaxMajorStep && aSpacing * step < kMinMajorPx)
            step *= 2;
        return step;
    }

    int Scaler::pixelWidth(int aFrame) const {
        // Pure linear map: the keying system is decoupled from the frame
        // range, so keys may live at any frame (before 0 or past maxFrame)
        // and must render where they are, not projected onto the range edge.
        // Rounded, because the rate is fractional below 1 px/frame (THE LADDER):
        // two adjacent frames then land on the same pixel, which is what zooming
        // out is. Clamped, because the rate is no longer bounded above either: a
        // large frame at a large zoom could ask for more pixels than an int holds.
        constexpr double kMaxPixelWidth = 1.0e9;
        const double wanted = std::lround(pixelsPerFrame() * aFrame);
        return static_cast<int>(std::clamp(wanted, -kMaxPixelWidth, kMaxPixelWidth));
    }

    int Scaler::maxPixelWidth() const { return pixelWidth(mFrameEnd); }

    int Scaler::frame(int aPixelWidth) const {
        // Unbounded inverse of pixelWidth: a mouse position outside the frame
        // range maps to an out-of-range frame, so keys can be created, moved
        // and selected beyond the range. The playback range (Current) clamps
        // its own values; this scale never does. The clamp is pixelWidth's,
        // the other way round.
        constexpr double kMaxFrame = 1.0e9;
        const double wanted = std::lround(aPixelWidth / pixelsPerFrame());
        return static_cast<int>(std::clamp(wanted, -kMaxFrame, kMaxFrame));
    }

    int Scaler::frameEnd() const { return mFrameEnd; }

    // The spacing comes from pixelsPerFrame(): the index is not a rate, and pos feeds every
    // published position - the ruler's ticks and numbers, the keys, the range markers.
    Scaler::Attribute Scaler::attribute(int aFrame) const { return attributeFor(aFrame, pixelsPerFrame()); }

    Scaler::Attribute Scaler::attributeAt(int aFrame, double aSpacing) const { return attributeFor(aFrame, aSpacing); }

    Scaler::Attribute Scaler::attributeFor(int aFrame, double aSpacing) const {
        Attribute attr;
        attr.pos = static_cast<int>(std::lround(aSpacing * aFrame));

        // A frame wider than a number carries its own: every frame is a numbered major. Without this
        // the grid can be COARSER than the view - the finest rung is 3 frames for 24 fps, i.e. 270 px
        // at 90 px/frame - so nothing lands in the viewport and the ruler draws nothing at all.
        if (aSpacing >= kNumberSpacing) {
            attr.level = TickLevel::Major;
            attr.showNumber = true;
            return attr;
        }

        // One intermediate level: the largest proper divisor of the major
        // step (step / least prime factor) — the rung directly below the
        // major in the divisor chain: 24 -> 12, 30 -> 15, 15 -> 5, 49 -> 7.
        // It sits at roughly half the major's on-screen spacing (majors stay
        // at 72-128px, so the rung lands 36-64px, near the 48px target) and
        // divides the major step by construction, so ticks always align.
        // Prime steps have no intermediate; the per-frame tier below is the
        // only other minor level (two minors max).
        const int step = majorStep(aSpacing);
        const int tier = step / leastPrimeFactor(step);

        if (aFrame % step == 0) {
            attr.level = TickLevel::Major;
            attr.showNumber = true;
        } else if (tier > 1 && aFrame % tier == 0 && aSpacing * tier >= 16.0) {
            // Number the intermediate tier only once it has room on screen,
            // so zooming in reveals progressively finer frame numbers.
            attr.level = TickLevel::Intermediate;
            attr.showNumber = (aSpacing * tier >= kNumberSpacing);
        } else {
            // Per-frame minor ticks only once frames have room to read as
            // distinct lines; any closer and they smear into a solid band.
            attr.level = aSpacing >= 6.0 ? TickLevel::Minor : TickLevel::None;
            attr.showNumber = false;
        }
        return attr;
    }

} // namespace time
} // namespace ctrl
