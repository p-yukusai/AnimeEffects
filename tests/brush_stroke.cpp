// Gate for the shared brush model (ctrl/Brush.h): the falloff every target
// deposits with, the stroke sampler that turns cursor samples into spaced
// stamps with interpolated pressure, and the axis latch the size/hardness drag
// uses. Headless: no widgets, no GL, no project.
//
// Why this is a gate rather than a detail of one tool: every painting context
// (raster masks, the FFD mesh warp, bone influence / weight painting, pose
// erasing) reads these functions, so a change here changes all of them - and
// the defect that motivated the sampler is invisible in a single-stamp test
// (stamping once per drag event left fast strokes as dots). The drag latch is
// here for the same reason: a vertical drag once changed the brush's size too.
#include <QPointF>
#include <QVector2D>
#include <cmath>
#include <cstdio>
#include <vector>

// Nothing here touches XCAssert, but Brush.h pulls util/NonCopyable only; the
// test needs no assert sink.
#include "ctrl/Brush.h"

namespace {

using ctrl::brushFalloff;

int failures = 0;
void check(bool aOk, const char* aWhat) {
    if (!aOk) {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}
bool nearly(float a, float b, float aEpsilon = 1.0e-5f) { return std::fabs(a - b) <= aEpsilon; }

ctrl::BrushParam makeParam(float aRadius, float aHardness, float aSpacing) {
    ctrl::BrushParam param;
    param.radius = aRadius;
    param.hardness = aHardness;
    param.spacing = aSpacing;
    param.flow = 1.0f;
    param.opacity = 1.0f;
    param.pressureSize = false;
    param.pressureFlow = true;
    return param;
}

struct Collected {
    std::vector<ctrl::BrushStamp> stamps;
    void operator()(const ctrl::BrushStamp& aStamp) { stamps.push_back(aStamp); }
};

} // namespace

int main() {
    // ------------------------------------------------------------- [1] falloff
    {
        // hardness 1: a hard disc - full strength everywhere inside, nothing at the rim
        check(brushFalloff(0.0f, 10.0f, 1.0f) == 1.0f, "hardness 1 is full strength at the centre");
        check(brushFalloff(9.999f, 10.0f, 1.0f) == 1.0f, "hardness 1 stays full strength to the rim");
        check(brushFalloff(10.0f, 10.0f, 1.0f) == 0.0f, "the rim itself weighs nothing");
        check(brushFalloff(12.0f, 10.0f, 1.0f) == 0.0f, "outside the radius weighs nothing");

        // hardness 0: a fully feathered disc - smoothstep, 1 at the centre, 0.5 at the
        // half radius (smoothstep(0.5) == 0.5), 0 at the rim
        check(brushFalloff(0.0f, 10.0f, 0.0f) == 1.0f, "hardness 0 is full strength at the centre");
        check(nearly(brushFalloff(5.0f, 10.0f, 0.0f), 0.5f), "hardness 0 is half strength at half the radius");
        check(brushFalloff(10.0f, 10.0f, 0.0f) == 0.0f, "hardness 0 ends at the rim");

        // hardness 0.5: a plateau to the half radius, then the band
        check(brushFalloff(4.0f, 10.0f, 0.5f) == 1.0f, "the core is at full strength for hardness 0.5");
        check(nearly(brushFalloff(7.5f, 10.0f, 0.5f), 0.5f), "the band's midpoint is half strength");
        check(brushFalloff(10.0f, 10.0f, 0.5f) == 0.0f, "the band ends at the rim");

        // monotone and inside [0, 1] over the whole disc
        float previous = 1.0f;
        bool monotone = true;
        bool bounded = true;
        for (int i = 0; i <= 200; ++i) {
            const float d = 0.05f * float(i);
            const float w = brushFalloff(d, 10.0f, 0.4f);
            if (w > previous + 1.0e-6f)
                monotone = false;
            if (w < 0.0f || w > 1.0f)
                bounded = false;
            previous = w;
        }
        check(monotone, "the falloff never rises with distance");
        check(bounded, "the falloff stays inside [0, 1]");

        // a degenerate brush deposits nothing rather than dividing by zero
        check(brushFalloff(0.0f, 0.0f, 0.5f) == 0.0f, "a zero radius weighs nothing");
        check(brushFalloff(0.0f, 10.0f, 2.0f) == 1.0f, "a hardness above 1 is clamped, not undefined");

        if (failures == 0)
            std::printf("[1] the shared falloff (plateau + smoothstep)\n");
    }

    // -------------------------------------------------------------- [2] spacing
    {
        ctrl::BrushStroke stroke;
        Collected collected;
        const float radius = 10.0f;
        const float step = radius * 0.25f; // 2.5 px
        stroke.begin(makeParam(radius, 0.5f, 0.25f), QVector2D(0.0f, 0.0f), 1.0f, [&](const ctrl::BrushStamp& s) {
            collected(s);
        });
        check(collected.stamps.size() == 1, "the press point stamps immediately");
        stroke.move(QVector2D(100.0f, 0.0f), 1.0f);

        check(collected.stamps.size() >= 40, "a 100px move at a 10px radius and 0.25 spacing stamps every 2.5px");
        float maxGap = 0.0f;
        float maxOffAxis = 0.0f;
        for (size_t i = 1; i < collected.stamps.size(); ++i) {
            const QVector2D delta = collected.stamps[i].center - collected.stamps[i - 1].center;
            maxGap = std::max(maxGap, delta.length());
            maxOffAxis = std::max(maxOffAxis, std::fabs(collected.stamps[i].center.y()));
        }
        std::printf("MEASURED spacing stamps=%d maxGap=%.3f (step %.3f)\n", int(collected.stamps.size()), maxGap, step);
        check(maxGap <= step + 1.0e-3f, "no gap between stamps is larger than the step");
        check(maxOffAxis <= 1.0e-4f, "the stamps stay on the segment");
        check(nearly(collected.stamps.back().center.x(), 100.0f, step), "the last stamp lands at the end of the move");

        // consecutive moves must continue the same spacing across the event boundary
        const size_t before = collected.stamps.size();
        stroke.move(QVector2D(200.0f, 0.0f), 1.0f);
        check(collected.stamps.size() > before, "a second move keeps stamping");
        const float crossGap =
            (collected.stamps[before].center - collected.stamps[before - 1].center).length();
        check(crossGap <= step + 1.0e-3f, "the spacing carries across drag events (no gap, no double stamp)");

        stroke.end();
        const size_t atEnd = collected.stamps.size();
        stroke.move(QVector2D(300.0f, 0.0f), 1.0f);
        check(collected.stamps.size() == atEnd, "an ended stroke emits nothing");
        check(!stroke.isActive(), "end() deactivates the stroke");

        if (failures == 0)
            std::printf("[2] stroke spacing, continuous across events\n");
    }

    // ------------------------------------------------------------- [3] pressure
    {
        ctrl::BrushStroke stroke;
        Collected collected;
        stroke.begin(makeParam(20.0f, 0.5f, 0.5f), QVector2D(0.0f, 0.0f), 0.2f, [&](const ctrl::BrushStamp& s) {
            collected(s);
        });
        stroke.move(QVector2D(60.0f, 0.0f), 1.0f);

        bool rising = true;
        bool bounded = true;
        for (size_t i = 1; i < collected.stamps.size(); ++i) {
            if (collected.stamps[i].amount < collected.stamps[i - 1].amount - 1.0e-6f)
                rising = false;
            if (collected.stamps[i].amount < 0.0f || collected.stamps[i].amount > 1.0f)
                bounded = false;
        }
        std::printf(
            "MEASURED pressure stamps=%d amount %.3f -> %.3f\n", int(collected.stamps.size()),
            collected.stamps.empty() ? -1.0f : collected.stamps.front().amount,
            collected.stamps.empty() ? -1.0f : collected.stamps.back().amount
        );
        check(!collected.stamps.empty(), "the pressure stroke stamped");
        check(nearly(collected.stamps.front().amount, 0.2f), "the first stamp uses the press pressure");
        check(rising, "a pressure ramp raises the amount monotonically");
        check(bounded, "the amount stays inside [0, 1]");
        check(nearly(collected.stamps.back().amount, 1.0f, 0.02f), "the last stamp reaches the pen's pressure");

        // pressure drives SIZE only when asked to
        ctrl::BrushParam sized = makeParam(20.0f, 0.5f, 0.5f);
        sized.pressureSize = true;
        ctrl::BrushStroke sizedStroke;
        Collected sizedStamps;
        sizedStroke.begin(sized, QVector2D(0.0f, 0.0f), 0.25f, [&](const ctrl::BrushStamp& s) { sizedStamps(s); });
        check(nearly(sizedStamps.stamps.front().radius, 5.0f), "pressureSize scales the radius with pressure");
        check(nearly(sizedStamps.stamps.front().amount, 0.25f), "pressureFlow is independent of pressureSize");

        ctrl::BrushStroke fixedStroke;
        Collected fixed;
        ctrl::BrushParam fixedParam = makeParam(20.0f, 0.5f, 0.5f);
        fixedParam.pressureFlow = false;
        fixedStroke.begin(fixedParam, QVector2D(0.0f, 0.0f), 0.25f, [&](const ctrl::BrushStamp& s) { fixed(s); });
        check(nearly(fixed.stamps.front().amount, 1.0f), "pressureFlow off makes the amount ignore pressure");
        check(nearly(fixed.stamps.front().radius, 20.0f), "pressureSize off keeps the radius");

        if (failures == 0)
            std::printf("[3] pressure ramps and the two pressure switches\n");
    }

    // --------------------------------------------------------------- [4] edges
    {
        ctrl::BrushStroke stroke;
        Collected collected;
        stroke.begin(makeParam(10.0f, 0.5f, 0.25f), QVector2D(5.0f, 5.0f), 1.0f, [&](const ctrl::BrushStamp& s) {
            collected(s);
        });
        stroke.move(QVector2D(5.0f, 5.0f), 0.5f); // no travel
        check(collected.stamps.size() == 1, "a move with no travel stamps nothing");
        check(nearly(collected.stamps.front().amount, 1.0f), "the press stamp keeps its own pressure");

        // the step is floored, so a hairline brush cannot stamp thousands of times per pixel
        ctrl::BrushParam hairline = makeParam(0.1f, 0.5f, 0.125f);
        check(nearly(hairline.stampStep(), 0.5f), "the stamp step is floored at half a pixel");

        // erase is carried through both channels
        ctrl::BrushParam erase = makeParam(10.0f, 0.5f, 0.25f);
        erase.erase = true;
        ctrl::BrushStroke eraseStroke;
        Collected erasing;
        eraseStroke.begin(erase, QVector2D(0.0f, 0.0f), 1.0f, [&](const ctrl::BrushStamp& s) { erasing(s); });
        check(erasing.stamps.size() == 1 && erasing.stamps.front().erase, "the erase flag reaches the target");
        check(nearly(erasing.stamps.front().hardness, 0.5f), "the stamp carries the hardness");

        if (failures == 0)
            std::printf("[4] zero-travel, the step floor and the erase flag\n");
    }

    // -------------------------------------------------- [5] size/hardness drag
    {
        using ctrl::BrushDrag;

        std::printf(
            "MEASURED drag slop=%.1fpx size=%.1fpx/step hardness=%.1fpx/step\n",
            double(BrushDrag::kAxisSlop), double(BrushDrag::kSizePxPerStep),
            double(BrushDrag::kHardnessPxPerStep)
        );
        check(nearly(BrushDrag::kAxisSlop, 4.0f), "the slop is the house 4px");
        check(nearly(BrushDrag::kSizePxPerStep, 8.0f), "the size rate is the old handler's 8px");
        check(nearly(BrushDrag::kHardnessPxPerStep, 40.0f), "the hardness rate is the old handler's 40px");

        // THE DIRECTION OF EACH MOVE DECIDES, AND ONLY ONE VALUE MOVES PER MOVE.
        // Reported twice: the old handler banked BOTH axes (a vertical drag changed the size too),
        // and the latch that replaced it could not turn at all ("it should not be axis locked,
        // though it should only change the axis of the direction the mouse is moving in at a
        // moment"). A near-vertical drag therefore moves the hardness and nothing else...
        {
            BrushDrag drag;
            drag.begin(QPointF(100.0, 100.0));
            QPointF pos(100.0, 100.0);
            float sizes = 0.0f;
            float hardness = 0.0f;
            for (int i = 0; i < 4; ++i) {
                pos += QPointF(2.0, -40.0);
                float s = 0.0f;
                float h = 0.0f;
                drag.move(pos, s, h);
                sizes += s;
                hardness += h;
            }
            std::printf(
                "MEASURED drag vertical size=%.3f hardness=%.3f\n", double(sizes), double(hardness)
            );
            check(drag.axis() == BrushDrag::Axis::Hardness, "a vertical move is the hardness");
            check(nearly(hardness, 3.0f), "40px of vertical travel is one hardness step");
            check(nearly(sizes, 0.0f), "and 2px of sideways jitter moves no size at all");
        }

        // ...and the mirror image, with the same 2px of cross-talk ignored the other way
        {
            BrushDrag drag;
            drag.begin(QPointF(0.0, 0.0));
            QPointF pos(0.0, 0.0);
            float sizes = 0.0f;
            float hardness = 0.0f;
            for (int i = 0; i < 4; ++i) {
                pos += QPointF(8.0, 2.0);
                float s = 0.0f;
                float h = 0.0f;
                drag.move(pos, s, h);
                sizes += s;
                hardness += h;
            }
            std::printf(
                "MEASURED drag horizontal size=%.3f hardness=%.3f\n", double(sizes), double(hardness)
            );
            check(drag.axis() == BrushDrag::Axis::Size, "a horizontal move is the size");
            check(nearly(sizes, 3.0f), "8px of horizontal travel is one size step");
            check(nearly(hardness, 0.0f), "and 2px of vertical jitter moves no hardness");
        }

        // A GESTURE CAN TURN. A vertical stretch followed by a horizontal one moves the hardness
        // and THEN the size - the latch's impossibility, and the reason it was removed.
        {
            BrushDrag drag;
            drag.begin(QPointF(0.0, 0.0));
            float s = 0.0f;
            float h = 0.0f;
            drag.move(QPointF(0.0, -40.0), s, h);   // the slop move, applies nothing
            drag.move(QPointF(0.0, -80.0), s, h);   // vertical: hardness
            const float hardOnly = h;
            float sizes = 0.0f;
            for (int i = 1; i <= 4; ++i) {
                drag.move(QPointF(8.0 * float(i), -80.0), s, h);
                sizes += s;
            }
            std::printf(
                "MEASURED drag turn hardness=%.3f then size=%.3f axis=%d\n", double(hardOnly),
                double(sizes), int(drag.axis())
            );
            check(nearly(hardOnly, 1.0f), "the vertical leg moved the hardness");
            check(nearly(sizes, 4.0f), "and the horizontal leg that followed moved the size - no latch");
            check(drag.axis() == BrushDrag::Axis::Size, "the axis is the LAST move's, not the first's");
        }

        // NOTHING MOVES INSIDE THE SLOP: a click with Alt must not change a value
        {
            BrushDrag drag;
            drag.begin(QPointF(0.0, 0.0));
            const QPointF underSlop[] = {
                QPointF(1.0, 1.0), QPointF(2.0, 2.0), QPointF(3.0, 3.0), QPointF(3.0, 0.0)
            };
            bool fired = false;
            float total = 0.0f;
            for (const QPointF& point : underSlop) {
                float s = 0.0f;
                float h = 0.0f;
                if (drag.move(point, s, h))
                    fired = true;
                total += s + h;
            }
            check(!fired, "a drag inside the slop fires nothing at all");
            check(nearly(total, 0.0f), "and produces no steps");
            check(!drag.latched(), "and the gesture has not declared itself (the eyedropper's click)");
        }

        // THE RUN-UP IS DISCARDED, NOT BANKED: the move that leaves the slop applies nothing.
        {
            BrushDrag drag;
            drag.begin(QPointF(0.0, 0.0));
            float s = 0.0f;
            float h = 0.0f;
            const bool fired = drag.move(QPointF(0.0, -100.0), s, h);
            check(!fired && h == 0.0f, "the move that leaves the slop applies nothing");
            drag.move(QPointF(0.0, -120.0), s, h);
            std::printf("MEASURED drag run-up hardness=%.3f over 120px of travel\n", double(h));
            check(nearly(h, 0.5f), "20px past the run-up is half a step, not 2.5 (the run-up did not count)");
        }

        // CONTINUOUS, NOT QUANTISED: the fraction of a notch is REPORTED as a fraction, so the
        // caller can apply it. This is what stops the hardness snapping in 5% jumps.
        {
            BrushDrag drag;
            drag.begin(QPointF(0.0, 0.0));
            float s = 0.0f;
            float h = 0.0f;
            drag.move(QPointF(0.0, -5.0), s, h);   // leaves the slop: applies nothing
            drag.move(QPointF(0.0, -15.0), s, h);  // 10px of travel
            std::printf("MEASURED drag fractional hardness=%.3f of a step\n", double(h));
            check(nearly(h, 0.25f), "10px of vertical travel is a QUARTER of a hardness step");
            drag.move(QPointF(9.0, -15.0), s, h);
            check(nearly(s, 1.125f), "and 9px horizontal is one and an eighth of a size step");
        }

        // the vertical sense is the one the gesture always had: up raises it
        {
            BrushDrag drag;
            drag.begin(QPointF(0.0, 0.0));
            float s = 0.0f;
            float h = 0.0f;
            drag.move(QPointF(0.0, -7.0), s, h);    // leaves the slop
            drag.move(QPointF(0.0, -47.0), s, h);
            check(h > 0.0f, "dragging up raises the hardness");
            drag.move(QPointF(0.0, -37.0), s, h);
            check(h < 0.0f, "dragging back down lowers it");
            drag.end();
            const bool endedFired = drag.move(QPointF(0.0, -500.0), s, h);
            check(!drag.isActive(), "end() deactivates the drag");
            check(!endedFired && s == 0.0f && h == 0.0f, "an ended drag ignores moves");
        }

        if (failures == 0)
            std::printf("[5] the size/hardness drag: per-move axis, continuous steps, no latch\n");
    }

    if (failures == 0)
        std::printf("brush_stroke: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
