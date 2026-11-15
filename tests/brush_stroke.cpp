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

        // A NEAR-VERTICAL DRAG CHANGES THE HARDNESS AND NOTHING ELSE. 2px of sideways
        // travel per 40px of vertical is the reported annoyance; the old handler
        // banked both components. The first move latches the axis and its run-up is
        // discarded, so four moves produce three steps.
        {
            BrushDrag drag;
            drag.begin(QPointF(100.0, 100.0));
            QPointF pos(100.0, 100.0);
            int sizes = 0;
            int hardness = 0;
            for (int i = 0; i < 4; ++i) {
                pos += QPointF(2.0, -40.0);
                int s = 0;
                int h = 0;
                drag.move(pos, s, h);
                sizes += s;
                hardness += h;
            }
            std::printf(
                "MEASURED drag vertical size=%d hardness=%d sizeBank=%.3f\n", sizes, hardness,
                double(drag.sizeBank())
            );
            check(drag.axis() == BrushDrag::Axis::Hardness, "a vertical drag latches the hardness axis");
            check(hardness == 3, "a vertical drag fires one hardness step per 40px after the latch");
            check(sizes == 0, "a vertical drag fires no size step");
            check(nearly(drag.sizeBank(), 0.0f), "a vertical drag never banks the size");
        }

        // and the mirror image: a near-horizontal drag changes the size only
        {
            BrushDrag drag;
            drag.begin(QPointF(0.0, 0.0));
            QPointF pos(0.0, 0.0);
            int sizes = 0;
            int hardness = 0;
            for (int i = 0; i < 4; ++i) {
                pos += QPointF(8.0, 2.0);
                int s = 0;
                int h = 0;
                drag.move(pos, s, h);
                sizes += s;
                hardness += h;
            }
            std::printf(
                "MEASURED drag horizontal size=%d hardness=%d hardnessBank=%.3f\n", sizes, hardness,
                double(drag.hardnessBank())
            );
            check(drag.axis() == BrushDrag::Axis::Size, "a horizontal drag latches the size axis");
            check(sizes == 3, "a horizontal drag fires one size step per 8px after the latch");
            check(hardness == 0, "a horizontal drag fires no hardness step");
            check(nearly(drag.hardnessBank(), 0.0f), "a horizontal drag never banks the hardness");
        }

        // A DRAG INSIDE THE SLOP DOES NOTHING AT ALL. The test is the largest single
        // component, measured from the PRESS point, so a diagonal of 3px each way
        // (4.24px long) is still inside it, and exactly 4px does not trip it either.
        {
            BrushDrag drag;
            drag.begin(QPointF(0.0, 0.0));
            const QPointF underSlop[] = {QPointF(1.0, 1.0), QPointF(2.0, 2.0), QPointF(3.0, 3.0), QPointF(4.0, 0.0)};
            int sizes = 0;
            int hardness = 0;
            bool fired = false;
            for (const QPointF& point : underSlop) {
                int s = 0;
                int h = 0;
                if (drag.move(point, s, h))
                    fired = true;
                sizes += s;
                hardness += h;
            }
            std::printf(
                "MEASURED drag slop size=%d hardness=%d axis=%d sizeBank=%.3f hardnessBank=%.3f\n", sizes,
                hardness, int(drag.axis()), double(drag.sizeBank()), double(drag.hardnessBank())
            );
            check(!fired, "a drag inside the slop fires nothing at all");
            check(drag.axis() == BrushDrag::Axis::None, "a drag inside the slop latches nothing");
            check(sizes == 0 && hardness == 0, "a drag inside the slop produces no steps");
            check(nearly(drag.sizeBank(), 0.0f) && nearly(drag.hardnessBank(), 0.0f), "the banks stay empty inside the slop");
        }

        // ONCE LATCHED THE AXIS HOLDS. A vertical latch followed by 120px of pure
        // horizontal travel would have moved the size before; now it fires nothing.
        {
            BrushDrag drag;
            drag.begin(QPointF(0.0, 0.0));
            int s = 0;
            int h = 0;
            drag.move(QPointF(0.0, -20.0), s, h); // latches the hardness, fires nothing
            int sizes = 0;
            int hardness = 0;
            for (int i = 1; i <= 3; ++i) {
                int ms = 0;
                int mh = 0;
                drag.move(QPointF(40.0 * float(i), -20.0), ms, mh);
                sizes += ms;
                hardness += mh;
            }
            std::printf("MEASURED drag latched size=%d hardness=%d axis=%d\n", sizes, hardness, int(drag.axis()));
            check(drag.axis() == BrushDrag::Axis::Hardness, "the axis does not re-evaluate mid-drag");
            check(sizes == 0 && hardness == 0, "after a vertical latch, horizontal moves fire nothing");
            check(nearly(drag.sizeBank(), 0.0f), "the unlatched axis keeps an empty bank");
        }

        // THE RUN-UP IS DISCARDED, NOT BANKED: 100px of travel that latched must not
        // hand the first step to the next 20px.
        {
            BrushDrag drag;
            drag.begin(QPointF(0.0, 0.0));
            int s = 0;
            int h = 0;
            const bool latchFired = drag.move(QPointF(0.0, -100.0), s, h);
            std::printf(
                "MEASURED drag run-up latchFired=%d hardnessBank=%.3f\n", int(latchFired),
                double(drag.hardnessBank())
            );
            check(!latchFired && h == 0, "the move that latches fires no step");
            check(nearly(drag.hardnessBank(), 0.0f), "the run-up travel is discarded, not banked");

            int hardness = 0;
            drag.move(QPointF(0.0, -120.0), s, h);
            hardness += h;
            check(h == 0, "half a step past the latch still fires nothing");

            drag.move(QPointF(0.0, -140.0), s, h);
            hardness += h;
            std::printf("MEASURED drag run-up steps=%d over 140px of travel\n", hardness);
            check(hardness == 1, "140px of travel fires one 40px step: the 100px run-up did not count");
        }

        // THE RATES ARE THE OLD ONES and the remainder carries, so a slow drag still
        // reaches the same value a fast one does.
        {
            BrushDrag drag;
            drag.begin(QPointF(0.0, 0.0));
            int s = 0;
            int h = 0;
            drag.move(QPointF(8.0, 0.0), s, h); // latches the size, run-up discarded
            int sizes = 0;
            drag.move(QPointF(16.0, 0.0), s, h);
            sizes += s;
            drag.move(QPointF(24.0, 0.0), s, h);
            sizes += s;
            std::printf("MEASURED drag rate 8px+8px size=%d bank=%.3f\n", sizes, double(drag.sizeBank()));
            check(sizes == 2, "8px then 8px fires two size steps");

            BrushDrag partial;
            partial.begin(QPointF(0.0, 0.0));
            partial.move(QPointF(8.0, 0.0), s, h); // latches the size
            int partialSizes = 0;
            partial.move(QPointF(15.0, 0.0), s, h);
            partialSizes += s;
            check(partialSizes == 0, "a 7px move inside a latched drag fires nothing");
            check(nearly(partial.sizeBank(), 0.875f), "a 7px move keeps its 7/8 of a step");
            partial.move(QPointF(16.0, 0.0), s, h);
            partialSizes += s;
            std::printf(
                "MEASURED drag remainder 7px+1px size=%d bank=%.3f\n", partialSizes,
                double(partial.sizeBank())
            );
            check(partialSizes == 1, "the carried remainder completes the step");
            check(nearly(partial.sizeBank(), 0.0f), "the fired step leaves no residue");

            BrushDrag hard;
            hard.begin(QPointF(0.0, 0.0));
            hard.move(QPointF(0.0, -5.0), s, h); // latches the hardness, run-up discarded
            hard.move(QPointF(0.0, -45.0), s, h);
            std::printf("MEASURED drag rate 40px hardness=%d\n", h);
            check(h == 1, "40px of the latched axis is one hardness step");

            // the vertical sense is the one the gesture always had: up raises it
            hard.move(QPointF(0.0, -5.0), s, h);
            check(h == -1, "dragging back down takes a hardness step off");

            hard.end();
            int afterEnd = 0;
            const bool endedFired = hard.move(QPointF(0.0, -500.0), s, afterEnd);
            check(!hard.isActive(), "end() deactivates the drag");
            check(!endedFired && s == 0 && afterEnd == 0, "an ended drag ignores moves");
        }

        if (failures == 0)
            std::printf("[5] the size/hardness drag: slop, latch and the old rates\n");
    }

    if (failures == 0)
        std::printf("brush_stroke: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
