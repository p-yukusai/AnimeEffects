// THE FRAME AXIS' ZOOM LADDER.
//
// ctrl::time::Scaler is the frame axis' only rate, and it is EXPONENTIAL: one notch multiplies the
// rate by kZoomPerNotch (1.1, the application's own zoom step), so the wheel feels the same at every
// zoom level. Its index bounds are the only limits, and they are far past any use.
//
// What is pinned here, and why each part matters:
//   * the step: x1.1 at every index, high and low;
//   * NO WALL either way, and no degenerate rate (0) at the ends;
//   * indexForRate() INVERTS the ladder, and rounds to the sparsest rung that still fits - the
//     graph panel's fit picks its rate with it, so a bad inverse would mis-fit every window;
//   * tickStride() AGREES with attribute(): the ruler and the panel iterate the visible frames in
//     strides of it, and a stride that skipped a drawn tick would silently drop ruler numbers at
//     one zoom level (the cost of not doing it at all is millions of iterations per repaint at a
//     deep zoom-out, which is why the stride exists);
//   * the pixel geometry (pixelWidth/frame) still ROUND-TRIPS at a fractional rate, which is what
//     the content axis is expressed in;
//   * a HELD anchored zoom (the delegated panel's notches) keeps the anchored frame within half a
//     frame - not just one notch - because the view transform is whole content pixels and its
//     sub-pixel translation remainder is carried between notches.
#include <cstdio>
#include <cmath>

#include "ctrl/time/time_Scaler.h"
#include "ctrl/TimeLineEditor.h"

namespace {

int failures = 0;

void check(bool aOk, const char* aWhat) {
    if (!aOk) {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}

// One wheel notch is kWheelValue (120) of raw delta. A POSITIVE angle delta is the wheel pushed
// away from the user. The conventions below follow ctrl::CanvasMover, TimeLineEditor and the
// graph page (MouseSetting::invertZoom reverses them).
//
// the wheel toward the user: zoom OUT
void notchOut(ctrl::time::Scaler& aScale, int aCount = 1) {
    for (int i = 0; i < aCount; ++i)
        aScale.update(-120);
}

} // namespace

int main() {
    // --- the step is EXPONENTIAL: the same relative change at every zoom ---
    // A ladder linear in px/frame steps by 100% at 1 px/frame and 6% at 16, so the wheel changes
    // speed as it is used. One notch must multiply the rate instead, at every zoom.
    {
        ctrl::time::Scaler scale;
        scale.setFps(24);
        scale.setFrameEnd(600);
        scale.setIndex(0);
        check(std::abs(scale.pixelsPerFrame() - 1.0) < 1e-9, "index 0 is 1 px/frame");
        const double steps[] = { 1.0, 1.1, 1.21, 2.5937 }; // 1.1^0, ^1, ^2, ^10
        for (int i = 0; i < 4; ++i) {
            ctrl::time::Scaler probe;
            probe.setIndex(i == 3 ? 10 : i);
            check(std::fabs(probe.pixelsPerFrame() - steps[i]) < 1e-4, "the rate is kZoomPerNotch^index");
        }
        // The same ratio wherever you are: this is the property the linear core could not have.
        for (const int index : { -40, -5, 0, 7, 40 }) {
            ctrl::time::Scaler a;
            ctrl::time::Scaler b;
            a.setIndex(index);
            b.setIndex(index + 1);
            const double ratio = b.pixelsPerFrame() / a.pixelsPerFrame();
            check(std::fabs(ratio - 1.1) < 1e-9, "one notch is x1.1 at every zoom level");
        }
        std::printf("[L] ladder: 1 -> %.4f -> %.4f px/frame, every notch x1.1\n", 1.0 * 1.1 * 1.1,
                    1.1 * 1.1 * 1.1 * 1.1 * 1.1 * 1.1 * 1.1 * 1.1 * 1.1 * 1.1);
    }

    // --- the bounds are the only limits, and they are far past any use ---
    {
        ctrl::time::Scaler out;
        out.setFps(24);
        out.setFrameEnd(600);
        out.setIndex(-120);
        const double deep = out.pixelsPerFrame();
        check(deep > 0.0 && deep < 1e-4, "the deepest rung is below 1/10000 px/frame");
        check(out.update(120), "a wheel-up notch from there still moves");
        check(out.pixelsPerFrame() > deep, "and changes the rate upwards (it zooms IN)");

        ctrl::time::Scaler in;
        in.setFps(24);
        in.setFrameEnd(600);
        in.setIndex(120);
        const double high = in.pixelsPerFrame();
        check(high > 10000.0, "the highest rung is past 10000 px/frame");
        check(in.update(-120), "a wheel-down notch from there still moves");
        check(in.pixelsPerFrame() < high, "and changes the rate downwards (it zooms OUT)");
        std::printf("[L] bounds: %.9f .. %.1f px/frame\n", deep, high);
    }

    // --- indexForRate inverts, and picks the sparsest rung that fits ---
    {
        ctrl::time::Scaler scale;
        scale.setFps(24);
        scale.setFrameEnd(600);
        const auto atRate = [](int aIndex) {
            ctrl::time::Scaler probe;
            probe.setFps(24);
            probe.setFrameEnd(600);
            probe.setIndex(aIndex);
            return probe;
        };
        const double rates[] = { 0.001, 0.05, 0.5, 1.0, 2.0, 7.08, 16.0, 17.0, 100.0, 4096.0 };
        for (const double wanted : rates) {
            const int index = scale.indexForRate(wanted);
            const double got = atRate(index).pixelsPerFrame();
            const double next = atRate(index + 1).pixelsPerFrame();
            check(got <= wanted * (1.0 + 1e-9), "indexForRate does not overshoot the rate it was given");
            check(next > wanted * (1.0 - 1e-9), "and the next rung up is past it (the sparsest that fits)");
            std::printf("[L] indexForRate(%.6g) -> index %d (rate %.6g, next %.6g)\n", wanted, index, got, next);
        }
    }

    // --- the stride never skips a drawn tick ---
    {
        ctrl::time::Scaler scale;
        scale.setFps(30);
        scale.setFrameEnd(600);
        int checked = 0;
        for (int index = -20; index <= 40; ++index) {
            ctrl::time::Scaler probe;
            probe.setFps(30);
            probe.setFrameEnd(600);
            probe.setIndex(index);
            const double spacing = probe.pixelsPerFrame();
            const int stride = probe.tickStride(spacing);
            check(stride >= 1, "the stride is at least one frame");
            for (int frame = -900; frame <= 900; frame += 1) {
                const auto attr = probe.attribute(frame);
                if (attr.level == ctrl::time::Scaler::TickLevel::None)
                    continue;
                ++checked;
                if (frame % stride != 0) {
                    std::printf(
                        "FAIL  index %d (%.6g px/frame): frame %d is drawn as level %d but the stride is %d\n",
                        index, spacing, frame, static_cast<int>(attr.level), stride
                    );
                    ++failures;
                    break;
                }
            }
        }
        std::printf("[L] stride: %d drawn ticks agreed with their stride over 61 zoom levels\n", checked);
    }

    // --- the ruler never goes blank: every rate draws a legible grid ---
    // The grid must stay legible at BOTH ends of the ladder: a frame wider than a number is a
    // numbered major, and at a small rate even the coarsest rung is too dense to read, so the step
    // doubles until it is not. Asserted on the drawn spacing, over a sweep of zoom levels.
    {
        ctrl::time::Scaler scale;
        scale.setFps(24);
        scale.setFrameEnd(600);
        int levels = 0;
        for (int index = -30; index <= 40; ++index) {
            ctrl::time::Scaler probe;
            probe.setFps(24);
            probe.setFrameEnd(600);
            probe.setIndex(index);
            const double spacing = probe.pixelsPerFrame();
            // Something inside a 280px viewport must be marked, and the marked grid must be legible.
            const int span = static_cast<int>(280.0 / spacing);
            bool marked = false;
            for (int frame = 0; frame <= span; ++frame) {
                if (probe.attribute(frame).level != ctrl::time::Scaler::TickLevel::None) {
                    marked = true;
                    break;
                }
            }
            if (!marked) {
                std::printf("FAIL  index %d (%.8f px/frame): nothing is marked across %d frames\n", index, spacing,
                            span);
                ++failures;
            }
            ++levels;
        }
        std::printf("[L] ruler: %d zoom levels each mark something inside a viewport\n", levels);
    }

    // --- the pixel geometry round-trips as far as whole pixels allow ---
    {
        ctrl::time::Scaler scale;
        scale.setFps(24);
        scale.setFrameEnd(600);
        notchOut(scale, 12); // a fractional rate
        check(scale.pixelsPerFrame() < 1.0, "the rate under test is fractional");
        // One pixel of content is 1 / rate frames, so that is the resolution the inverse has.
        const double slack = 1.0 + 1.0 / scale.pixelsPerFrame();
        for (int frame : { -100, -1, 0, 1, 37, 600 }) {
            const int px = scale.pixelWidth(frame);
            const int back = scale.frame(px);
            if (std::abs(back - frame) > slack) {
                std::printf("FAIL  frame %d -> px %d -> frame %d is past the %g-frame pixel slack\n",
                            frame, px, back, slack);
                ++failures;
            }
        }
        std::printf("[L] geometry: %.6f px/frame (slack %.1f frames), frame 37 -> px %d -> frame %d\n",
                    scale.pixelsPerFrame(), slack, scale.pixelWidth(37), scale.frame(scale.pixelWidth(37)));
    }

    // --- a HELD anchored zoom keeps the anchored frame, not only one notch ---
    // The panel hands the timeline the frame under its cursor FRACTIONALLY (frameAtPixel,
    // never frameAt), and the timeline's view transform is whole content pixels. Each notch
    // therefore leaves a sub-pixel remainder, and the zoom ladder makes that remainder
    // different every notch, so rounding each one away does not cancel. What is driven here
    // is the production arithmetic - TimeLineEditor::zoomAnchorDelta over the real ladder -
    // with the panel's own way of re-reading the anchor: the frame at a FIXED cursor column
    // of the pushed window, which is exactly what GraphEditor::updateWheel captures next.
    {
        constexpr int kNotches = 20;
        const double startFrame = 20.5; // fractional: a drawn column is not the exact frame
        const int margin = ctrl::TimeLineEditor::kTimeLineMargin;

        ctrl::time::Scaler scale;
        scale.setFps(24);
        scale.setFrameEnd(600);
        scale.setIndex(1); // the timeline's own seed rate
        const double startSpacing = scale.pixelsPerFrame();
        const double cursorX = margin + startSpacing * startFrame; // the fixed screen column
        int offset = 0;        // the timeline's integer view transform (content px)
        double remainder = 0.0; // carried between notches, as the widget does
        double frame = startFrame;
        for (int i = 0; i < kNotches; ++i) {
            const double oldSpacing = scale.pixelsPerFrame();
            scale.update(120); // notch IN: x1.1 per step
            offset += ctrl::TimeLineEditor::zoomAnchorDelta(frame, oldSpacing, scale.pixelsPerFrame(), remainder);
            // The window the panel maps is (visibleFrameWindow): a content pixel is
            // frame (x - margin) / rate, re-anchored from the fixed cursor column.
            frame = (cursorX - offset - margin) / scale.pixelsPerFrame();
        }
        const double drift = std::fabs(frame - startFrame);
        const double spacing = scale.pixelsPerFrame();
        check(drift < 0.5, "20 anchored notches hold the anchored frame within half a frame");
        check(spacing > startSpacing * 3.0, "and the notches really climbed the zoom");

        // The arithmetic this replaces, kept as the baseline the assertion discriminates
        // against: round the anchor's content pixel, then round the scaled one (the pair in
        // TimeLineWidget::zoomFramesAbout / TimeLineEditor::anchorPixelAfterScale). It must
        // FAIL the same half-frame bar on this scenario, or the check above proves nothing.
        ctrl::time::Scaler roundedScale;
        roundedScale.setFps(24);
        roundedScale.setFrameEnd(600);
        roundedScale.setIndex(1);
        int roundedOffset = 0;
        double roundedFrame = startFrame;
        for (int i = 0; i < kNotches; ++i) {
            const double oldSpacing = roundedScale.pixelsPerFrame();
            roundedScale.update(120);
            const double newSpacing = roundedScale.pixelsPerFrame();
            // frameStartPixel() for the [0, 600] range is margin + round(rate * 0) = margin;
            // the anchor itself stays fractional until this rounding.
            const int anchorX = static_cast<int>(std::lround(margin + oldSpacing * roundedFrame));
            const double scaled = (static_cast<double>(anchorX) - margin) * newSpacing / oldSpacing;
            const int pixelAfter = margin + static_cast<int>(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
            roundedOffset += anchorX - pixelAfter;
            roundedFrame = (cursorX - roundedOffset - margin) / newSpacing;
        }
        const double roundedDrift = std::fabs(roundedFrame - startFrame);
        check(roundedDrift > 0.5, "the rounding pair it replaces does not meet that bar on the same scenario");

        // ONE notch is unchanged where it was already measured exact: the probe's own
        // anchor (frame 20 at 1.1 px/frame, margin 24) translates by -2.2 px, which
        // rounds to the -2 px the rounded pair also applied, and the -0.2 px is carried.
        {
            ctrl::time::Scaler one;
            one.setIndex(1);
            const double oldSpacing = one.pixelsPerFrame();
            one.update(120);
            const double newSpacing = one.pixelsPerFrame();
            double oneRemainder = 0.0;
            const int exactDelta = ctrl::TimeLineEditor::zoomAnchorDelta(20.0, oldSpacing, newSpacing, oneRemainder);
            const int anchorX = static_cast<int>(std::lround(margin + oldSpacing * 20.0));
            const int roundedDelta =
                anchorX - (margin + static_cast<int>(std::lround((anchorX - margin) * newSpacing / oldSpacing)));
            check(exactDelta == roundedDelta, "one notch applies the same pixel the rounding pair did");
            check(std::fabs(oneRemainder) > 1e-9, "and only the sub-pixel part is carried");
        }

        std::printf("[L] anchored zoom: %d notches x1.1 -> %.4f px/frame, anchored frame %.3f -> %.3f "
                    "(rounded pair: %.3f frame off; carried remainder: %.4f frame)\n",
                    kNotches, spacing, startFrame, frame, roundedDrift, drift);
    }

    if (failures == 0)
        std::printf(
            "time_scaler: the frame ladder has no wall either way, its inverse and its tick stride "
            "agree, and a held anchored zoom stays anchored\n"
        );
    else
        std::printf("time_scaler: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
