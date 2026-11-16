// Regression test for the custom-bezier easing solver (src/util/Easing.cpp).
//
// The solver used to be Newton-only (guess t = x, up to 6 steps, break when
// |dx| < 1e-8, no bisection fallback, clamp t to [-2,2]). It was replaced by a
// port of WebKit's UnitBezier (WebCore/platform/graphics/UnitBezier.h):
// precomputed polynomial coefficients + an 11-point spline for the initial
// guess and bracket, up to 4 Newton steps that check the actual x-residual,
// then a bisection fallback. The old solver diverged on curves whose
// x-derivative approaches zero (e.g. control set (1,0,0,1): max error ~0.26).
//
// This test asserts the port matches an independent high-accuracy reference over
// the well-defined x in [0,1] domain, that Custom ignores weight while analytic
// easings still honor it, and that the header-exposed solver is correct and
// safe to call from the app's worker threads (it is a pure function of its
// control points). It exercises the solver directly (util::UnitBezier) as well
// as through util::Easing::calculate.
#include <QCoreApplication>
#include <QDebug>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include "util/Easing.h"
#include "util/EasingSegment.h"
#include "util/UnitBezier.h"
#include "XCAssert.h"

namespace {
class AbortHandler : public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class NullErrorHandler : public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override {}
};
} // namespace

static AbortHandler gAbortHandler;
static NullErrorHandler gNullErrorHandler;
XCAssertHandler* gXCAssertHandler = &gAbortHandler;
XCErrorHandler* gXCErrorHandler = &gNullErrorHandler;

// Independent high-accuracy reference for the x->y mapping over x in [0,1].
// Deliberately not derived from the repo's UnitBezier, so a bad port fails.
static double refSolve(double x, double x1, double y1, double x2, double y2) {
    const double cx = 3.0 * x1;
    const double bx = 3.0 * (x2 - x1) - cx;
    const double ax = 1.0 - cx - bx;
    const double cy = 3.0 * y1;
    const double by = 3.0 * (y2 - y1) - cy;
    const double ay = 1.0 - cy - by;
    auto sx = [&](double t) { return ((ax * t + bx) * t + cx) * t; };
    auto sd = [&](double t) { return (3.0 * ax * t + 2.0 * bx) * t + cx; };
    auto sy = [&](double t) { return ((ay * t + by) * t + cy) * t; };
    // Bisection to bracket the root, then Newton to refine. Bisection keeps the
    // solve correct near zero x-derivative where Newton alone diverges.
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 100; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (sx(mid) < x)
            lo = mid;
        else
            hi = mid;
        if (hi - lo < 1e-16)
            break;
    }
    double t = 0.5 * (lo + hi);
    for (int i = 0; i < 50; ++i) {
        const double r = sx(t) - x;
        const double d = sd(t);
        if (std::abs(r) < 1e-14)
            break;
        if (std::abs(d) < 1e-14)
            break;
        t -= r / d;
        if (t < 0.0)
            t = 0.0;
        else if (t > 1.0)
            t = 1.0;
    }
    return sy(t);
}

// Evaluate the repo's custom-bezier easing at normalized progress t. weight
// pairs with the curve/linear output blend inside calculateBezier.
static double evalCurve(double t, double x1, double y1, double x2, double y2, float weight = 1.0f) {
    util::Easing::Param p;
    p.type = util::Easing::Type_Custom;
    p.range = util::Easing::Range_InOut;
    p.weight = weight;
    p.cubicBezier.x1 = float(x1);
    p.cubicBezier.y1 = float(y1);
    p.cubicBezier.x2 = float(x2);
    p.cubicBezier.y2 = float(y2);
    return double(util::Easing::calculate(p, float(t), 0.0f, 1.0f, 1.0f));
}

struct Curve {
    double x1, y1, x2, y2;
    const char* name;
};

static double tolFor(double ref) { return 1e-4 + 1e-4 * std::fabs(ref); }

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const Curve curves[] = {
        {0.0, 0.0, 1.0, 1.0, "linear"},
        {2.0 / 3, 0.0, 1.0 / 3, 1.0, "app-default ease-in-out"},
        {0.42, 0.0, 0.58, 1.0, "css ease-in-out"},
        {0.25, 0.1, 0.25, 1.0, "css ease"},
        {0.42, 0.0, 1.0, 1.0, "css ease-in"},
        {0.0, 0.0, 0.58, 1.0, "css ease-out"},
        {0.5, 0.0, 0.5, 1.0, "vertical (Newton-hard)"},
        {1.0, 0.0, 0.0, 1.0, "extreme cross"}, // old solver erred ~0.26 here
        {0.9, 0.1, 0.1, 0.9, "shallow s"},
    };
    const int numCurves = int(sizeof(curves) / sizeof(curves[0]));

    // 1) Solver correctness over the well-defined domain.
    for (const Curve& c : curves) {
        for (int k = 0; k <= 200; ++k) {
            const double t = double(k) / 200.0;
            const double ref = refSolve(t, c.x1, c.y1, c.x2, c.y2);
            const double got = evalCurve(t, c.x1, c.y1, c.x2, c.y2);
            if (std::fabs(got - ref) > tolFor(ref)) {
                XC_MSG_ASSERT(false, "curve %s t=%.3f got=%.6f ref=%.6f err=%.2e",
                              c.name, t, got, ref, std::fabs(got - ref));
            }
        }
    }

    // 2) Weight semantics: for a Custom bezier, weight is ignored -- a drawn
    //    curve is exactly that curve regardless of the Param's weight (the
    //    control is disabled in the UI as redundant). The analytic easing path
    //    still blends weight as curve<->linear.
    {
        const Curve& wc = curves[1]; // app-default ease-in-out
        for (int k = 0; k <= 100; ++k) {
            const double t = double(k) / 100.0;
            const double ref = refSolve(t, wc.x1, wc.y1, wc.x2, wc.y2);
            for (float w : {0.0f, 0.5f, 1.0f}) {
                const double v = evalCurve(t, wc.x1, wc.y1, wc.x2, wc.y2, w);
                XC_ASSERT(std::fabs(v - ref) <= tolFor(ref));
            }
        }
        // Analytic easings still honor weight (eased*w + linear*(1-w)).
        util::Easing::Param p;
        p.type = util::Easing::Type_Cubic;
        p.range = util::Easing::Range_InOut;
        for (int k = 0; k <= 100; ++k) {
            const double t = double(k) / 100.0;
            p.weight = 1.0f;
            const double curve = double(util::Easing::calculate(p, float(t), 0.0f, 1.0f, 1.0f));
            p.weight = 0.0f;
            const double lin = double(util::Easing::calculate(p, float(t), 0.0f, 1.0f, 1.0f));
            p.weight = 0.5f;
            const double mid = double(util::Easing::calculate(p, float(t), 0.0f, 1.0f, 1.0f));
            XC_ASSERT(std::fabs(lin - t) <= 1e-5);
            XC_ASSERT(std::fabs(mid - 0.5 * (curve + t)) <= 1e-5);
        }
    }

    // 3) Direct UnitBezier tests: the solver is now an independent,
    //    header-exposed type. Verify it head-to-head against the high-accuracy
    //    reference, plus a few closed-form landmarks.
    {
        for (const Curve& c : curves) {
            const util::UnitBezier ub(c.x1, c.y1, c.x2, c.y2);
            XC_ASSERT(std::fabs(ub.solve(0.0, util::UnitBezier::kBezierEpsilon) - 0.0) <= 1e-7);
            XC_ASSERT(std::fabs(ub.solve(1.0, util::UnitBezier::kBezierEpsilon) - 1.0) <= 1e-7);
            for (int k = 1; k < 100; ++k) {
                const double x = double(k) / 100.0;
                const double ref = refSolve(x, c.x1, c.y1, c.x2, c.y2);
                const double got = ub.solve(x, util::UnitBezier::kBezierEpsilon);
                XC_ASSERT(std::fabs(got - ref) <= tolFor(ref));
            }
        }
        // Control set (0,0,1,1): x(t)=y(t)=3t^2-2t^3, so the solve is the identity.
        const util::UnitBezier lin(0.0, 0.0, 1.0, 1.0);
        XC_ASSERT(std::fabs(lin.solve(0.3, util::UnitBezier::kBezierEpsilon) - 0.3) <= 1e-7);
        XC_ASSERT(std::fabs(lin.solve(0.7, util::UnitBezier::kBezierEpsilon) - 0.7) <= 1e-7);
        // Overshoot controls rise above y=1 inside the interval.
        const util::UnitBezier over(0.4, 1.5, 0.6, 1.5);
        XC_ASSERT(over.solve(0.5, util::UnitBezier::kBezierEpsilon) > 1.0);
    }

    // 4) The solver is a pure function of its control points (no shared mutable
    //    state), so it is safe to call from the app's worker threads. Stress it
    //    concurrently and require every result to match the reference.
    {
        const int kThreads = 4;
        const int kIters = 2000;
        std::atomic<int> failures{0};
        std::vector<std::thread> threads;
        for (int th = 0; th < kThreads; ++th) {
            threads.emplace_back([&, th]() {
                for (int i = 0; i < kIters; ++i) {
                    const Curve& c = curves[(th + i) % numCurves];
                    const double t = double(i % 100) / 100.0;
                    const double ref = refSolve(t, c.x1, c.y1, c.x2, c.y2);
                    const util::UnitBezier ub(c.x1, c.y1, c.x2, c.y2);
                    const double got = ub.solve(t, util::UnitBezier::kBezierEpsilon);
                    if (std::fabs(got - ref) > tolFor(ref))
                        ++failures;
                }
            });
        }
        for (auto& th : threads)
            th.join();
        XC_MSG_ASSERT(failures.load() == 0, "threaded stress failed %d times", int(failures.load()));
    }

    // 5) Param value-equality includes the bezier controls: two Params that
    //    differ only in cubicBezier are NOT equal (the change-detection stamp
    //    in EasingItem relies on this). Identical controls are equal.
    {
        util::Easing::Param a;
        a.type = util::Easing::Type_Custom;
        a.range = util::Easing::Range_InOut;
        a.weight = 1.0f;
        a.cubicBezier = util::Easing::CubicBezier{0.4f, 0.2f, 0.6f, 0.8f};

        util::Easing::Param same = a;
        util::Easing::Param diff = a;
        diff.cubicBezier.x1 = 0.45f;

        XC_ASSERT(same == a);
        XC_ASSERT(diff != a);
        XC_ASSERT(a.cubicBezier == same.cubicBezier);
        XC_ASSERT(!(a.cubicBezier == diff.cubicBezier));
    }

    // 6) PER-SIDE HANDLES: a Custom curve's two control points are derived by
    //    their OWN HandleMode. These cases pin the derivation itself, which is
    //    the reference's PER-KEY central difference (easing-demo.html,
    //    computeAllTangents' default 'uniform' mode), normalized to the segment:
    //      m = (next.y - prev.y) / (next.x - prev.x)
    //    and the handle at each third is y = m/3 (outgoing) or 1 - m/3
    //    (incoming). Each expected value below is worked out by hand:
    //      * BOTH neighbours: the FROM key's central difference over prev and
    //        TO, and the TO key's over FROM and next;
    //      * a key with only ONE neighbour follows Blender's endpoint rule and
    //        borrows the adjacent key's own third-point slope: an open FROM end
    //        gets 3/2 - m_TO/2, an open TO end 3/2 - m_FROM/2;
    //      * a two-key channel has no interior key to borrow from, so it is the
    //        chord (m = 1, the identity thirds);
    //      * AutoClamped intersects the key's slope with the bounds that keep
    //        BOTH of its handles inside its three-key value range.
    //    A side's mode decides only WHETHER it derives; the slope it uses is the
    //    key's, shared with the other side, which is what makes a symmetric
    //    neighbourhood draw a symmetric pair (the block after the table).
    {
        const float span = 10.0f; // the segment 0 -> 10, values 0 -> 1

        // A key is a "key of the segment" here: frame offset from the from key,
        // value in units of the segment's own change.
        auto make = [&](bool aHasPrev, float aPrevFrame, float aPrevValue,
                        bool aHasNext, float aNextFrame, float aNextValue) {
            util::Easing::AutoContext ctx;
            ctx.prev.valid = aHasPrev;
            ctx.prev.frame = aPrevFrame;
            ctx.prev.value = aPrevValue;
            ctx.next.valid = aHasNext;
            ctx.next.frame = aNextFrame;
            ctx.next.value = aNextValue;
            ctx.inMode = util::Easing::HandleMode_Auto;
            return ctx;
        };

        struct HandleCase {
            util::Easing::AutoContext ctx;
            util::Easing::HandleMode outMode;
            float y1, y2;
            const char* name;
        };
        const HandleCase cases[] = {
            // No neighbours at all: the segment says nothing about its own shape,
            // so both sides fall back to the diagonal's thirds and the curve IS
            // Linear.
            {make(false, 0, 0, false, 0, 0), util::Easing::HandleMode_Auto,
             1.0f / 3.0f, 2.0f / 3.0f, "isolated segment"},
            // Both neighbours, both sides Auto. prev at frame -10 is px = -1,
            // next at 25 is nx = 2.5; py = -0.5, ny = 2.
            //   outgoing (FROM): m = (1-(-0.5))/(1-(-1)) = 0.75 -> y1 = 0.25
            //   incoming (TO):   m = (2-0)/(2.5-0) = 0.8 -> y2 = 1 - 0.8/3 = 11/15
            {make(true, -10.0f, -0.5f, true, 25.0f, 2.0f), util::Easing::HandleMode_Auto,
             0.25f, 11.0f / 15.0f, "both neighbours"},
            // Only a next key (nx = 2, ny = 3). TO's own slope is
            // m_TO = (3-0)/(2-0) = 1.5. The open FROM end borrows it:
            //   outgoing m = 3/2 - 1.5/2 = 0.75 -> y1 = 0.25
            //   incoming m = 1.5 -> y2 = 1 - 1.5/3 = 0.5
            {make(false, 0, 0, true, 20.0f, 3.0f), util::Easing::HandleMode_Auto,
             0.25f, 0.5f, "first key of the channel"},
            // Only a previous key (px = -0.5, py = -1). FROM's slope is
            // m_FROM = (1-(-1))/(1-(-0.5)) = 4/3.
            //   outgoing m = 4/3 -> y1 = 4/9
            //   incoming m = 3/2 - (4/3)/2 = 5/6 -> y2 = 1 - 5/18 = 13/18
            {make(true, -5.0f, -1.0f, false, 0, 0), util::Easing::HandleMode_Auto,
             4.0f / 9.0f, 13.0f / 18.0f, "last key of the channel"},
            // A near, high previous key: FROM's raw central slope is
            // (1-2)/(1-(-0.1)) = -10/11. Auto's outgoing handle is
            // y = -10/33; AutoClamped intersects the slope with the bounds of
            // prev=2/from=0/to=1, which is [0, 0], so the extremum is flat and
            // the open TO end (borrowing the clamped FROM slope 0) is
            // 3/2 -> y2 = 0.5.
            {make(true, -1.0f, 2.0f, false, 0, 0), util::Easing::HandleMode_AutoClamped,
             0.0f, 0.5f, "clamped start"},
        };

        for (const HandleCase& c : cases) {
            util::Easing::Param p;
            p.type = util::Easing::Type_Custom;
            p.handleOut = c.outMode;
            p.handleIn = util::Easing::HandleMode_Auto;
            p.cubicBezier = util::Easing::CubicBezier{0.4f, 0.1f, 0.8f, 0.9f};

            const QPointF c1 = util::Easing::controlPoint(p, c.ctx.inMode, c.ctx, span, 0);
            const QPointF c2 = util::Easing::controlPoint(p, c.ctx.inMode, c.ctx, span, 1);
            XC_MSG_ASSERT(std::fabs(c1.x() - 1.0 / 3.0) <= 1e-9, "%s: outgoing x %.6f", c.name, c1.x());
            XC_MSG_ASSERT(std::fabs(c2.x() - 2.0 / 3.0) <= 1e-9, "%s: incoming x %.6f", c.name, c2.x());
            XC_MSG_ASSERT(std::fabs(c1.y() - c.y1) <= 1e-6, "handles %s: y1=%.6f want %.6f",
                          c.name, c1.y(), double(c.y1));
            XC_MSG_ASSERT(std::fabs(c2.y() - c.y2) <= 1e-6, "handles %s: y2=%.6f want %.6f",
                          c.name, c2.y(), double(c.y2));

            // The evaluated curve must be the bezier those control points draw,
            // and it must pass through both keys (a pair that missed them would
            // make the segment jump).
            const util::UnitBezier expect(
                static_cast<float>(c1.x()), static_cast<float>(c1.y()),
                static_cast<float>(c2.x()), static_cast<float>(c2.y()));
            for (int k = 0; k <= 20; ++k) {
                const float x = float(k) / 20.0f;
                const double got = double(util::Easing::calculate(p, c.ctx, x * span, 0.0f, 1.0f, span));
                const double ref = expect.solve(double(x), util::UnitBezier::kBezierEpsilon);
                XC_MSG_ASSERT(std::fabs(got - ref) <= 1e-5, "auto solve %s x=%.2f got=%.6f ref=%.6f",
                              c.name, double(x), got, ref);
            }
            XC_ASSERT(std::fabs(util::Easing::calculate(p, c.ctx, 0.0f, 0.0f, 1.0f, span) - 0.0f) <= 1e-6);
            XC_ASSERT(std::fabs(util::Easing::calculate(p, c.ctx, span, 0.0f, 1.0f, span) - 1.0f) <= 1e-6);
        }

        // SYMMETRY. A neighbourhood mirrored about the segment's centre must
        // produce a mirrored pair: y1 == 1 - y2, for BOTH Auto and AutoClamped.
        // The mirror of prev (px, py) is (1 - px, 1 - py); with prev at frame
        // -span that puts next at frame 2*span, value 1 - py. Each fixture below
        // therefore has ONE central slope at both keys, m = (1 - py)/2, and the
        // two handles are (1/3, m/3) and (2/3, 1 - m/3).
        {
            const float py[] = { -0.5f, 1.5f, 0.3f, 2.0f, -2.0f };
            for (util::Easing::HandleMode mode :
                 { util::Easing::HandleMode_Auto, util::Easing::HandleMode_AutoClamped }) {
                for (float v : py) {
                    const auto ctx = make(true, -span, v, true, 2.0f * span, 1.0f - v);
                    util::Easing::Param p;
                    p.type = util::Easing::Type_Custom;
                    p.handleOut = mode;
                    p.handleIn = mode;
                    const QPointF c1 = util::Easing::controlPoint(p, mode, ctx, span, 0);
                    const QPointF c2 = util::Easing::controlPoint(p, mode, ctx, span, 1);
                    XC_MSG_ASSERT(std::fabs(c1.y() + c2.y() - 1.0) <= 1e-9,
                                  "symmetry mode %d py %.3f: y1=%.9f y2=%.9f",
                                  int(mode), double(v), c1.y(), c2.y());
                }
            }
        }

        // THE HAND-COMPUTED SYMMETRIC FIXTURES. The two neighbourhoods that
        // mirror each other:
        //   A: prev(-1, -0.5), next(2, 1.5) -> m = 0.75 at both keys
        //   B: prev(-1,  1.5), next(2, -0.5) -> m = -0.25 at both keys
        // Auto leaves them free, so B overshoots by construction (and mirrors):
        //   A -> (1/3, 1/4) and (2/3, 3/4)
        //   B -> (1/3, -1/12) and (2/3, 13/12)
        // AutoClamped flattens B's local extremum: (1/3, 0) and (2/3, 1).
        {
            util::Easing::Param p;
            p.type = util::Easing::Type_Custom;
            p.handleOut = util::Easing::HandleMode_Auto;
            const auto a = make(true, -span, -0.5f, true, 2.0f * span, 1.5f);
            const auto b = make(true, -span, 1.5f, true, 2.0f * span, -0.5f);
            const QPointF a1 = util::Easing::controlPoint(p, p.handleOut, a, span, 0);
            const QPointF a2 = util::Easing::controlPoint(p, p.handleOut, a, span, 1);
            const QPointF b1 = util::Easing::controlPoint(p, p.handleOut, b, span, 0);
            const QPointF b2 = util::Easing::controlPoint(p, p.handleOut, b, span, 1);
            XC_ASSERT(std::fabs(a1.y() - 0.25) <= 1e-9);
            XC_ASSERT(std::fabs(a2.y() - 0.75) <= 1e-9);
            XC_ASSERT(std::fabs(b1.y() - (-1.0 / 12.0)) <= 1e-9);
            XC_ASSERT(std::fabs(b2.y() - (13.0 / 12.0)) <= 1e-9);
            // The pair is a mirror, not two independent guesses.
            XC_ASSERT(std::fabs(a1.y() + a2.y() - 1.0) <= 1e-9);
            XC_ASSERT(std::fabs(b1.y() + b2.y() - 1.0) <= 1e-9);

            p.handleOut = util::Easing::HandleMode_AutoClamped;
            const QPointF ac1 = util::Easing::controlPoint(p, p.handleOut, a, span, 0);
            const QPointF ac2 = util::Easing::controlPoint(p, p.handleOut, a, span, 1);
            const QPointF bc1 = util::Easing::controlPoint(p, p.handleOut, b, span, 0);
            const QPointF bc2 = util::Easing::controlPoint(p, p.handleOut, b, span, 1);
            XC_ASSERT(std::fabs(ac1.y() - 0.25) <= 1e-9);
            XC_ASSERT(std::fabs(ac2.y() - 0.75) <= 1e-9);
            XC_ASSERT(std::fabs(bc1.y() - 0.0) <= 1e-9);
            XC_ASSERT(std::fabs(bc2.y() - 1.0) <= 1e-9);
            XC_ASSERT(std::fabs(bc1.y() + bc2.y() - 1.0) <= 1e-9);
        }

        // THE CLAMP IS THE ONLY DIFFERENCE between Auto and AutoClamped: the same
        // near, high previous key leaves Auto's outgoing control below the
        // segment (-10/33) while AutoClamped holds it at 0. A curve that
        // overshoots is exactly what a user picks Auto over AutoClamped for.
        {
            util::Easing::Param p;
            p.type = util::Easing::Type_Custom;
            p.handleOut = util::Easing::HandleMode_Auto;
            p.handleIn = util::Easing::HandleMode_Auto;
            const auto ctx = make(true, -1.0f, 2.0f, false, 0, 0);
            const QPointF c1 = util::Easing::controlPoint(p, ctx.inMode, ctx, span, 0);
            XC_ASSERT(std::fabs(c1.y() - (-10.0 / 33.0)) <= 1e-5);
            XC_ASSERT(c1.y() < 0.0f);
        }

        // THE USER'S QUESTION, at the math level: an AUTO side keeps deriving
        // while the OTHER side is what changed. Side 0 (outgoing) reads `prev`
        // only, side 1 (incoming) reads `next` only, so changing `next` moves
        // side 1 and leaves side 0 byte-identical.
        {
            util::Easing::Param p;
            p.type = util::Easing::Type_Custom;
            p.handleOut = util::Easing::HandleMode_Auto;
            p.handleIn = util::Easing::HandleMode_Auto;
            const auto ctxA = make(true, -10.0f, -0.5f, true, 25.0f, 2.0f);
            auto ctxB = ctxA;
            ctxB.next.value = 5.0f; // the neighbour AFTER the to key changed

            const QPointF outA = util::Easing::controlPoint(p, ctxA.inMode, ctxA, span, 0);
            const QPointF inA = util::Easing::controlPoint(p, ctxA.inMode, ctxA, span, 1);
            const QPointF outB = util::Easing::controlPoint(p, ctxB.inMode, ctxB, span, 0);
            const QPointF inB = util::Easing::controlPoint(p, ctxB.inMode, ctxB, span, 1);
            XC_ASSERT(outA == outB); // the untouched Auto side did not move
            XC_ASSERT(inA != inB);   // ... and the other one is STILL DERIVING
        }

        // A side's own mode decides its control point, and the arriving side's
        // mode comes from the TO key (aInMode), NEVER from aFrom.handleIn. A
        // stored side reads its stored control point even when the other side is
        // derived.
        {
            util::Easing::Param p;
            p.type = util::Easing::Type_Custom;
            p.handleOut = util::Easing::HandleMode_Free;
            p.handleIn = util::Easing::HandleMode_Auto; // the FROM key's incoming side: irrelevant here
            p.cubicBezier = util::Easing::CubicBezier{0.4f, 0.1f, 0.8f, 0.9f};
            const auto ctx = make(true, -10.0f, -0.5f, true, 25.0f, 2.0f);

            const QPointF c1 = util::Easing::controlPoint(p, util::Easing::HandleMode_Free, ctx, span, 0);
            const QPointF c2 = util::Easing::controlPoint(p, util::Easing::HandleMode_Free, ctx, span, 1);
            XC_ASSERT(c1 == QPointF(0.4f, 0.1f)); // stored: Free is the truth
            XC_ASSERT(c2 == QPointF(0.8f, 0.9f)); // stored via inMode, not p.handleIn

            const QPointF d2 = util::Easing::controlPoint(p, util::Easing::HandleMode_Auto, ctx, span, 1);
            XC_ASSERT(d2 != c2); // the same stored side derives when the TO key says Auto
        }

        // Vector is the segment's own chord: both sides sit on the diagonal, so
        // the segment is a straight line (Blender's Vector), whatever the
        // neighbours are.
        {
            util::Easing::Param p;
            p.type = util::Easing::Type_Custom;
            p.handleOut = util::Easing::HandleMode_Vector;
            p.handleIn = util::Easing::HandleMode_Vector;
            auto ctx = make(true, -10.0f, -0.5f, true, 25.0f, 2.0f);
            // The arriving side's mode is the TO key's, which calculate() reads
            // from aCtx.inMode - NOT from p.handleIn (that is the FROM key's own
            // incoming side, i.e. the PREVIOUS segment). With inMode Auto the
            // incoming handle would derive from `next` and the segment would not
            // be the straight chord this case asserts.
            ctx.inMode = util::Easing::HandleMode_Vector;
            const QPointF c1 = util::Easing::controlPoint(p, ctx.inMode, ctx, span, 0);
            const QPointF c2 = util::Easing::controlPoint(p, ctx.inMode, ctx, span, 1);
            XC_ASSERT(c1 == QPointF(1.0 / 3.0, 1.0 / 3.0));
            XC_ASSERT(c2 == QPointF(2.0 / 3.0, 2.0 / 3.0));
            for (int k = 0; k <= 10; ++k) {
                const float t = float(k) / 10.0f * span;
                XC_ASSERT(std::fabs(util::Easing::calculate(p, ctx, t, 0.0f, 1.0f, span) - t / span) <= 1e-5);
            }
        }

        // Weight and range are inert for a Custom curve, exactly as they were
        // before: the derived pair is the shape, and a stale weight (from
        // another type, or read out of a saved file) can never skew it.
        {
            const auto ctx = make(true, -10.0f, -0.5f, true, 25.0f, 2.0f);
            util::Easing::Param base;
            base.type = util::Easing::Type_Custom;
            base.handleOut = util::Easing::HandleMode_Auto;
            base.handleIn = util::Easing::HandleMode_Auto;
            for (float w : {0.0f, 0.25f, 1.0f}) {
                util::Easing::Param p = base;
                p.weight = w;
                for (int r = 0; r < util::Easing::Range_TERM; ++r) {
                    p.range = util::Easing::Range(r);
                    for (int k = 0; k <= 10; ++k) {
                        const float t = float(k) / 10.0f * span;
                        const double a = util::Easing::calculate(p, ctx, t, 0.0f, 1.0f, span);
                        const double b = util::Easing::calculate(base, ctx, t, 0.0f, 1.0f, span);
                        XC_ASSERT(std::fabs(a - b) <= 1e-6);
                    }
                }
            }
            // A Custom curve with both sides STORED is exactly its stored pair -
            // no context needed, which is the common path.
            util::Easing::Param stored = base;
            stored.handleOut = util::Easing::HandleMode_Free;
            stored.handleIn = util::Easing::HandleMode_Free;
            stored.cubicBezier = util::Easing::CubicBezier{0.4f, 0.2f, 0.6f, 0.8f};
            const util::UnitBezier bez(0.4f, 0.2f, 0.6f, 0.8f);
            for (int k = 0; k <= 10; ++k) {
                const float t = float(k) / 10.0f * span;
                const double got = util::Easing::calculate(stored, t, 0.0f, 1.0f, span);
                const double ref = bez.solve(double(t / span), util::UnitBezier::kBezierEpsilon);
                XC_ASSERT(std::fabs(got - ref) <= 1e-5);
            }
        }
    }

    // 7) THE USER-FACING NAME OF A PARAM. A non-Custom interpolation is named by
    //    getTypeName; a Custom curve is named by its TWO HANDLE MODES, because
    //    that is where its identity lives now that Type_Auto is retired. The five
    //    equal pairs get the mode's own name, a mixed pair names both sides in
    //    (incoming, outgoing) order.
    {
        auto nameOf = [](util::Easing::HandleMode aIn, util::Easing::HandleMode aOut) {
            util::Easing::Param p;
            p.type = util::Easing::Type_Custom;
            p.handleIn = aIn;
            p.handleOut = aOut;
            return util::Easing::getParamName(p);
        };
        const util::Easing::HandleMode modes[] = {
            util::Easing::HandleMode_Free, util::Easing::HandleMode_Aligned,
            util::Easing::HandleMode_Vector, util::Easing::HandleMode_Auto,
            util::Easing::HandleMode_AutoClamped,
        };
        for (util::Easing::HandleMode mode : modes) {
            const QString want = util::Easing::getHandleModeName(mode);
            XC_MSG_ASSERT(nameOf(mode, mode) == want, "both sides %d should read '%s', got '%s'",
                          int(mode), qPrintable(want), qPrintable(nameOf(mode, mode)));
        }
        XC_ASSERT(nameOf(util::Easing::HandleMode_Auto, util::Easing::HandleMode_Auto) ==
                  QStringLiteral("Auto"));
        XC_ASSERT(nameOf(util::Easing::HandleMode_AutoClamped, util::Easing::HandleMode_AutoClamped) ==
                  QStringLiteral("Auto Clamped"));
        XC_ASSERT(nameOf(util::Easing::HandleMode_Auto, util::Easing::HandleMode_Auto) !=
                  nameOf(util::Easing::HandleMode_AutoClamped, util::Easing::HandleMode_AutoClamped));
        // A mixed pair is NOT given one side's name - it names both, incoming first.
        const QString mixed = nameOf(util::Easing::HandleMode_Auto, util::Easing::HandleMode_Free);
        XC_ASSERT(mixed == util::Easing::getHandleModeName(util::Easing::HandleMode_Auto)
                      + QStringLiteral(" / ")
                      + util::Easing::getHandleModeName(util::Easing::HandleMode_Free));
        XC_ASSERT(mixed != util::Easing::getHandleModeName(util::Easing::HandleMode_Auto));
        // The interpolation vocabulary is untouched: a non-Custom Param still
        // answers its type's name (the chip's "Custom" bug was reading THIS for a
        // mode-shaped curve).
        util::Easing::Param cubic;
        cubic.type = util::Easing::Type_Cubic;
        XC_ASSERT(util::Easing::getParamName(cubic) == util::Easing::getTypeName(util::Easing::Type_Cubic));
        util::Easing::Param linear;
        XC_ASSERT(util::Easing::getParamName(linear) == util::Easing::getTypeName(util::Easing::Type_Linear));
    }

    // 8) THE DEFAULTS. An unedited Param is EXACTLY what it was before the
    //    per-side refactor: the linear curve with the stored smoothstep bezier and
    //    two linked sides. It is not automatic, and its evaluation cannot read a
    //    neighbour, so an unedited key cannot change shape when a neighbour moves.
    //    The application's automatic DEFAULT - a migrated 0.14 Auto, or a
    //    stored Auto default creating a key - is auto CLAMPED, and the explicit
    //    Auto PRESET (picker tile / header button / graph action) writes that same
    //    clamped pair. Unclamped Auto is only a deliberate per-side pick.
    {
        const util::Easing::Param fresh;
        XC_ASSERT(fresh.type == util::Easing::Type_Linear);
        XC_ASSERT(fresh.range == util::Easing::Range_InOut);
        XC_ASSERT(fresh.weight == 1.0f);
        XC_ASSERT(fresh.cubicBezier == util::Easing::CubicBezier());
        XC_ASSERT(fresh.handleIn == util::Easing::HandleMode_Aligned);
        XC_ASSERT(fresh.handleOut == util::Easing::HandleMode_Aligned);
        XC_ASSERT(!util::Easing::isDerived(fresh.handleIn));
        XC_ASSERT(!util::Easing::isDerived(fresh.handleOut));

        // Two entirely different neighbour pairs: the default curve is identical,
        // and it is the straight line. (A derived default would move with them.)
        util::Easing::AutoContext a;
        a.prev = { true, -1.0f, 3.0f };
        a.next = { true, 4.0f, -2.0f };
        a.inMode = util::Easing::kDefaultAutomaticHandleMode;
        util::Easing::AutoContext b;
        b.prev = { true, -7.0f, -5.0f };
        b.next = { true, 9.0f, 8.0f };
        b.inMode = util::Easing::HandleMode_Auto;
        const float span = 10.0f;
        for (int k = 0; k <= 20; ++k) {
            const float t = float(k) / 20.0f * span;
            const double linear = double(t / span);
            const double va = double(util::Easing::calculate(fresh, a, t, 0.0f, 1.0f, span));
            const double vb = double(util::Easing::calculate(fresh, b, t, 0.0f, 1.0f, span));
            XC_MSG_ASSERT(std::fabs(va - linear) <= 1e-6, "default ctx A t=%.2f got=%.6f", double(t), va);
            XC_ASSERT(std::fabs(va - vb) <= 1e-9);
        }

        // The application's automatic pair - the default AND the explicit preset -
        // is AutoClamped, not Auto: a near, high previous key would leave
        // unclamped Auto far below the segment (the section above measures
        // -10/33) while the clamped pair stays at 0.
        XC_ASSERT(util::Easing::kDefaultAutomaticHandleMode == util::Easing::HandleMode_AutoClamped);
        XC_ASSERT(util::Easing::kAutoPresetHandleMode == util::Easing::HandleMode_AutoClamped);
        util::Easing::Param preset;
        preset.type = util::Easing::Type_Custom;
        preset.handleIn = util::Easing::kDefaultAutomaticHandleMode;
        preset.handleOut = util::Easing::kDefaultAutomaticHandleMode;
        XC_ASSERT(util::Easing::getParamName(preset) ==
                  util::Easing::getHandleModeName(util::Easing::HandleMode_AutoClamped));

        util::Easing::AutoContext near;
        near.prev = { true, -1.0f, 2.0f };
        near.inMode = util::Easing::kDefaultAutomaticHandleMode;
        const QPointF clamped = util::Easing::controlPoint(preset, near.inMode, near, span, 0);
        XC_ASSERT(std::fabs(clamped.y() - 0.0f) <= 1e-6);
        preset.handleOut = util::Easing::HandleMode_Auto;
        const QPointF unclamped = util::Easing::controlPoint(preset, near.inMode, near, span, 0);
        XC_ASSERT(unclamped.y() < 0.0f); // the overshoot the default must not have
    }

    // 9) THE THIRDS FAST PATH. A Custom pair whose control points sit on the
    //    third points in x - P1 = (1/3, y1), P2 = (2/3, y2) - makes the cubic's
    //    x component the identity, x(t) = t, so Easing.cpp reads y straight off
    //    the cubic in x and never runs the x -> t solve (isThirdsPair /
    //    thirdsPairY). EVERY automatic segment is such a pair: controlPoint
    //    answers x = 1/3 and 2/3 for Vector, Auto and AutoClamped alike, and a
    //    frozen handle stores those same floats. The numbers must NOT move, and
    //    the pairs that are not the thirds must NOT be swallowed.
    //
    //    The fast path and the solve differ only by the float thirds: fl(1/3) is
    //    9.93e-9 above 1/3, so x(t) - t = 3t(1-t^2)*9.93e-9 <= 1.15e-8 - an order
    //    below kBezierEpsilon, which is why a 1e-7 bound is the right one to
    //    demand of the two.
    {
        const float span = 10.0f;

        // THE DERIVED PAIR. A symmetric neighbourhood gives one central slope on
        // both keys: prev at (-1, -0.2), next at (2, 1.2) mirror each other about
        // the segment, so m = (1 - (-0.2)) / (1 - (-1)) = 0.6 at both keys and
        // the handles are (1/3, m/3) = (1/3, 0.2) and (2/3, 1 - m/3) = (2/3, 0.8).
        util::Easing::AutoContext ctx;
        ctx.prev = { true, -span, -0.2f };
        ctx.next = { true, 2.0f * span, 1.2f };
        ctx.inMode = util::Easing::kDefaultAutomaticHandleMode;

        util::Easing::Param derived;
        derived.type = util::Easing::Type_Custom;
        derived.handleOut = util::Easing::kAutoPresetHandleMode;
        // Deliberately UNRELATED stored values: an automatic pair must come from
        // the neighbours, not from what an earlier edit left behind.
        derived.cubicBezier = util::Easing::CubicBezier{0.4f, 0.1f, 0.8f, 0.9f};

        const QPointF d1 = util::Easing::controlPoint(derived, ctx.inMode, ctx, span, 0);
        const QPointF d2 = util::Easing::controlPoint(derived, ctx.inMode, ctx, span, 1);
        // Exact float equality is what the fast path's own gate tests, so assert
        // exactly that after the narrowing - not an epsilon that would also pass
        // for a pair merely near the thirds.
        XC_MSG_ASSERT(float(d1.x()) == 1.0f / 3.0f && float(d2.x()) == 2.0f / 3.0f,
                      "derived pair is not the thirds: x1=%.9f x2=%.9f", d1.x(), d2.x());
        XC_MSG_ASSERT(float(d1.y()) == 0.2f && float(d2.y()) == 0.8f,
                      "derived pair y: y1=%.9f y2=%.9f (want 0.2 / 0.8)", d1.y(), d2.y());

        // The same pair as a STORED one, which is what the frozen-handle path and
        // the stored evaluation funnel see.
        util::Easing::Param stored = derived;
        stored.handleOut = util::Easing::HandleMode_Free;
        stored.handleIn = util::Easing::HandleMode_Free;
        stored.cubicBezier =
            util::Easing::CubicBezier{1.0f / 3.0f, 0.2f, 2.0f / 3.0f, 0.8f};

        // The oracle is the header's own solve - the tree's independent WebKit
        // port - and the closed form is written out textbook-style (a different
        // association order from the fast path's Horner form).
        const util::UnitBezier oracle(1.0f / 3.0f, 0.2f, 2.0f / 3.0f, 0.8f);
        const auto cubicAt = [](double aX, double aY1, double aY2) {
            return 3.0 * (1.0 - aX) * (1.0 - aX) * aX * aY1
                + 3.0 * (1.0 - aX) * aX * aX * aY2 + aX * aX * aX;
        };

        double maxVsSolve = 0.0, maxVsSolveDerived = 0.0, maxVsCubic = 0.0, maxFunnelGap = 0.0,
               maxFrozenGap = 0.0;
        for (int k = 0; k <= 200; ++k) {
            const float x = float(k) / 200.0f;
            // Stored funnel: Easing::calculate(Param, t, b, c, d).
            const double viaStored = evalCurve(double(x), 1.0 / 3.0, 0.2, 2.0 / 3.0, 0.8);
            // Derived funnel: Easing::calculate(Param, AutoContext, t, b, c, d).
            const double viaDerived =
                double(util::Easing::calculate(derived, ctx, x * span, 0.0f, 1.0f, span));
            // The FROZEN-handle shape: Free modes over a stored thirds pair, which
            // is what GraphEditor::setHandleMode leaves behind. It must play the
            // same numbers, which is exactly why freezing does not move the curve.
            const double viaFrozen =
                double(util::Easing::calculate(stored, x * span, 0.0f, 1.0f, span));
            const double solved = oracle.solve(double(x), util::UnitBezier::kBezierEpsilon);
            const double cubic = cubicAt(double(x), 0.2, 0.8);
            // The derived funnel's own x is t/d, and the float multiply/divide
            // round trip through the frame span moves it by up to 6e-8 - that is
            // the caller's arithmetic, not the curve's, so hold the derived value
            // to the oracle at the x it actually used.
            const float xin = (x * span) / span;
            const double solvedAt = oracle.solve(double(xin), util::UnitBezier::kBezierEpsilon);

            maxVsSolve = std::max(maxVsSolve, std::fabs(viaStored - solved));
            maxVsSolveDerived = std::max(maxVsSolveDerived, std::fabs(viaDerived - solvedAt));
            maxVsCubic = std::max(maxVsCubic, std::fabs(viaStored - cubic));
            maxFunnelGap = std::max(maxFunnelGap, std::fabs(viaStored - viaDerived));
            maxFrozenGap = std::max(maxFrozenGap, std::fabs(viaFrozen - viaStored));

            XC_MSG_ASSERT(std::fabs(viaFrozen - viaStored) <= 1e-6,
                          "frozen thirds pair x=%.3f frozen=%.9f stored=%.9f",
                          double(x), viaFrozen, viaStored);

            XC_MSG_ASSERT(std::fabs(viaStored - solved) <= 1e-7,
                          "thirds stored x=%.3f fast=%.9f solve=%.9f err=%.2e",
                          double(x), viaStored, solved, std::fabs(viaStored - solved));
            XC_MSG_ASSERT(std::fabs(viaDerived - solvedAt) <= 1e-7,
                          "thirds derived x=%.3f fast=%.9f solve=%.9f err=%.2e",
                          double(xin), viaDerived, solvedAt, std::fabs(viaDerived - solvedAt));
            // The fast path must be the CUBIC, not the smoothstep and not
            // something the solve would not have produced.
            XC_MSG_ASSERT(std::fabs(viaStored - cubic) <= 1e-7,
                          "thirds vs cubic x=%.3f fast=%.9f cubic=%.9f err=%.2e",
                          double(x), viaStored, cubic, std::fabs(viaStored - cubic));
            // Playback (stored) and the graph panel (derived) must not disagree.
            XC_MSG_ASSERT(std::fabs(viaStored - viaDerived) <= 1e-6,
                          "thirds funnel gap x=%.3f stored=%.9f derived=%.9f",
                          double(x), viaStored, viaDerived);
        }
        // Endpoints are exact on both funnels: a segment must still join its keys.
        XC_ASSERT(std::fabs(evalCurve(0.0, 1.0 / 3.0, 0.2, 2.0 / 3.0, 0.8) - 0.0) <= 1e-7);
        XC_ASSERT(std::fabs(evalCurve(1.0, 1.0 / 3.0, 0.2, 2.0 / 3.0, 0.8) - 1.0) <= 1e-7);
        XC_ASSERT(std::fabs(
            double(util::Easing::calculate(derived, ctx, 0.0f, 0.0f, 1.0f, span)) - 0.0) <= 1e-6);
        XC_ASSERT(std::fabs(
            double(util::Easing::calculate(derived, ctx, span, 0.0f, 1.0f, span)) - 1.0) <= 1e-6);

        std::printf("thirds fast path: |stored-solve| max %.3e, |derived-solve| max %.3e, "
                    "|fast-cubic| max %.3e, |stored-derived| max %.3e, |frozen-stored| max %.3e\n",
                    maxVsSolve, maxVsSolveDerived, maxVsCubic, maxFunnelGap, maxFrozenGap);
    }

    // 10) THE CROSSED DEFAULT PAIR keeps the solve. The stored CubicBezier default
    //     is x1 = 2/3, x2 = 1/3 - the controls CROSSED - so its x(t) is
    //     2t - 3t^2 + 2t^3, NOT t, and the fast path must not take it. What that
    //     costs is real: the pair's parametric y(t) is the smoothstep 3t^2 - 2t^3,
    //     and the UNCROSSED thirds with the same Ys (x(t) = t) give that same
    //     smoothstep as a function of x. The crossed curve is a different function
    //     of x, and the gap printed below - a tenth of the unit range - is what a
    //     gate that tested anything looser than exact thirds would erase.
    {
        const util::Easing::CubicBezier def;
        XC_MSG_ASSERT(def.x1 == 2.0f / 3.0f && def.x2 == 1.0f / 3.0f,
                      "the default pair is no longer crossed: x1=%.6f x2=%.6f",
                      double(def.x1), double(def.x2));
        const util::UnitBezier crossed(def.x1, def.y1, def.x2, def.y2);

        double maxMargin = 0.0, atX = 0.0, crossedAtX = 0.0, smoothstepAtX = 0.0;
        for (int k = 0; k <= 200; ++k) {
            const double x = double(k) / 200.0;
            const double got = evalCurve(x, 2.0 / 3.0, 0.0, 1.0 / 3.0, 1.0);
            const double solved = crossed.solve(x, util::UnitBezier::kBezierEpsilon);
            XC_MSG_ASSERT(std::fabs(got - solved) <= 1e-6,
                          "crossed x=%.3f got=%.9f solve=%.9f", x, got, solved);
            // The same Ys on the UNCROSSED thirds - the pair the fast path takes.
            // There x(t) = t, so the value is the smoothstep in x, and asserting
            // that closed form pins the fast path's own arithmetic on this exact
            // curve while the crossed pair is held to the solve beside it.
            const double uncrossed = evalCurve(x, 1.0 / 3.0, 0.0, 2.0 / 3.0, 1.0);
            const double smoothstep = 3.0 * x * x - 2.0 * x * x * x;
            XC_MSG_ASSERT(std::fabs(uncrossed - smoothstep) <= 1e-6,
                          "uncrossed thirds is not the smoothstep: x=%.3f got=%.9f want=%.9f",
                          x, uncrossed, smoothstep);
            const double margin = std::fabs(got - uncrossed);
            if (margin > maxMargin) {
                maxMargin = margin;
                atX = x;
                crossedAtX = got;
                smoothstepAtX = uncrossed;
            }
        }
        // Pinned from both sides. A swallowed crossed pair would return the
        // smoothstep instead: at x = 0.355 that is 0.288597 where the crossed
        // curve reads 0.171297, and at the mirror x = 0.645 it is 0.711403 where
        // the crossed curve reads 0.828703 (the scan reports whichever of the two
        // edges the rounding favours). The margin would collapse to 0; today it
        // is 0.117301.
        std::printf("crossed default: max |crossed - smoothstep| = %.9f at x=%.3f "
                    "(crossed %.9f, smoothstep %.9f)\n",
                    maxMargin, atX, crossedAtX, smoothstepAtX);
        XC_MSG_ASSERT(maxMargin > 0.10 && maxMargin < 0.13,
                      "crossed/smoothstep margin %.9f at x=%.3f is outside [0.10, 0.13]",
                      maxMargin, atX);
        // The crossed curve is a real S: through the centre, below the smoothstep
        // over the first half and above it in the second, and symmetric about
        // (0.5, 0.5) - which is what "the smoothstep reparametrized" looks like.
        XC_ASSERT(std::fabs(evalCurve(0.5, 2.0 / 3.0, 0.0, 1.0 / 3.0, 1.0) - 0.5) <= 1e-6);
        XC_ASSERT(evalCurve(0.25, 2.0 / 3.0, 0.0, 1.0 / 3.0, 1.0) < 0.15625);
        XC_ASSERT(evalCurve(0.75, 2.0 / 3.0, 0.0, 1.0 / 3.0, 1.0) > 0.84375);
        XC_ASSERT(std::fabs(evalCurve(0.25, 2.0 / 3.0, 0.0, 1.0 / 3.0, 1.0)
                            + evalCurve(0.75, 2.0 / 3.0, 0.0, 1.0 / 3.0, 1.0) - 1.0) <= 1e-6);
    }

    // 11) AN ARBITRARY PAIR keeps the solve, through BOTH funnels. x = 0.2 / 0.8
    //     is nowhere near a third, so neither the stored evaluation nor a Free
    //     side of the derived one may take the fast path; the values must stay
    //     the solved ones. The solved curve is also measurably NOT the same-Y
    //     cubic in x - the identity assumption the fast path makes on the thirds
    //     is exactly what is false here, and the printed gap is the proof the
    //     solve is still doing that work.
    {
        const float span = 10.0f;
        const double x1 = 0.2, y1 = 0.1, x2 = 0.8, y2 = 0.45;
        const util::UnitBezier oracle(x1, y1, x2, y2);
        const auto cubicAt = [](double aX, double aY1, double aY2) {
            return 3.0 * (1.0 - aX) * (1.0 - aX) * aX * aY1
                + 3.0 * (1.0 - aX) * aX * aX * aY2 + aX * aX * aX;
        };

        // The derived funnel over a STORED pair: Free on both sides makes
        // controlPoint answer the stored points verbatim.
        util::Easing::Param stored;
        stored.type = util::Easing::Type_Custom;
        stored.handleIn = util::Easing::HandleMode_Free;
        stored.handleOut = util::Easing::HandleMode_Free;
        stored.cubicBezier = util::Easing::CubicBezier{
            float(x1), float(y1), float(x2), float(y2)};
        util::Easing::AutoContext ctx;
        ctx.prev = { true, -span, 2.0f }; // present but irrelevant: Free never derives
        ctx.next = { true, 2.0f * span, -1.0f };
        ctx.inMode = util::Easing::HandleMode_Free;

        double maxErr = 0.0, maxHermiteGap = 0.0;
        for (int k = 0; k <= 200; ++k) {
            const float x = float(k) / 200.0f;
            const double solved = oracle.solve(double(x), util::UnitBezier::kBezierEpsilon);
            const double viaStored = evalCurve(double(x), x1, y1, x2, y2);
            const double viaDerived =
                double(util::Easing::calculate(stored, ctx, x * span, 0.0f, 1.0f, span));
            maxErr = std::max(maxErr, std::max(std::fabs(viaStored - solved),
                                               std::fabs(viaDerived - solved)));
            maxHermiteGap = std::max(maxHermiteGap,
                                     std::fabs(viaStored - cubicAt(double(x), y1, y2)));
            XC_MSG_ASSERT(std::fabs(viaStored - solved) <= 1e-6,
                          "arbitrary stored x=%.3f got=%.9f solve=%.9f",
                          double(x), viaStored, solved);
            XC_MSG_ASSERT(std::fabs(viaDerived - solved) <= 1e-6,
                          "arbitrary derived x=%.3f got=%.9f solve=%.9f",
                          double(x), viaDerived, solved);
        }
        // ... and the pair is provably not being read as the cubic in x.
        std::printf("arbitrary pair (0.2, 0.1, 0.8, 0.45): |eval-solve| max %.3e, "
                    "|eval-cubic(x)| max %.3e\n",
                    maxErr, maxHermiteGap);
        XC_MSG_ASSERT(maxErr <= 1e-6, "arbitrary pair drifted from the solve by %.2e", maxErr);
        XC_MSG_ASSERT(maxHermiteGap > 1e-2,
                      "arbitrary pair read as the cubic in x - the gate is too loose "
                      "(gap %.2e)", maxHermiteGap);
    }

    // ---- WHICH BRANCH RAN, not merely "the numbers agree" -------------------------
    //
    // Every assertion above compares two values that are supposed to be equal, so a
    // build where the fast path never fires passes all of them. That cannot be pinned by
    // a tolerance: the two branches agree to their own rounding. It CAN be pinned
    // bit-for-bit, because for a thirds pair they do not always land on the same float -
    // measured: 379 of 999 samples differ by one ULP. So this sweep asserts EXACT
    // bitwise equality against the branch that must have run:
    //
    //   * a pair on the thirds must equal the DIRECT cubic y(x) (the fast path), and
    //   * a pair 1e-6 off the thirds must equal the SOLVER, not the cubic (the fast path
    //     is gated on the exact floats, so a near miss must not be read as one).
    //
    // The sweep first proves it CAN discriminate: if the two candidate values were
    // bitwise identical everywhere, both assertions below would be vacuous.
    {
        const float y1 = 0.2f, y2 = 0.8f;
        // Double throughout, INCLUDING the subtraction: `y2 - y1` in float arithmetic is
        // 0.6f, in double it is 0.6000000089406967, and that one rounding is enough to move
        // the result by an ULP for ~12% of samples. The shipped helper narrows the floats
        // once and then works in double, so this reference has to as well - a reference
        // that is merely "close" cannot tell two branches apart bit for bit.
        const double dy1 = double(y1), dy2 = double(y2);
        const double cy = 3.0 * dy1, by = 3.0 * (dy2 - dy1) - cy, ay = 1.0 - cy - by;
        const auto cubicInX = [&](double x) {
            return static_cast<float>(((ay * x + by) * x + cy) * x);
        };
        util::Easing::Param thirds;
        thirds.type = util::Easing::Type_Custom;
        thirds.cubicBezier = {1.0f / 3.0f, y1, 2.0f / 3.0f, y2};
        thirds.handleIn = util::Easing::HandleMode_Free;
        thirds.handleOut = util::Easing::HandleMode_Free;
        util::Easing::Param nearMiss = thirds;
        nearMiss.cubicBezier = {1.0f / 3.0f + 1e-6f, y1, 2.0f / 3.0f - 1e-6f, y2};

        int discriminating = 0;
        int thirdsExact = 0;
        int nearMatchesSolver = 0;
        int nearDiscriminating = 0;
        const util::UnitBezier solverThirds(1.0f / 3.0f, y1, 2.0f / 3.0f, y2);
        const util::UnitBezier solverNear(1.0f / 3.0f + 1e-6f, y1, 2.0f / 3.0f - 1e-6f, y2);
        for (int i = 1; i < 1000; ++i) {
            const float x = float(i) / 1000.0f;
            const float cubic = cubicInX(double(x));
            const float solved = static_cast<float>(solverThirds.solve(x, util::UnitBezier::kBezierEpsilon));
            if (cubic != solved) ++discriminating;

            const float gotThirds = util::Easing::calculate(thirds, x, 0.0f, 1.0f, 1.0f);
            if (gotThirds == cubic) ++thirdsExact;

            const float gotNear = util::Easing::calculate(nearMiss, x, 0.0f, 1.0f, 1.0f);
            const float solvedNear = static_cast<float>(solverNear.solve(x, util::UnitBezier::kBezierEpsilon));
            // The solver's answer, in EVERY sample: where the solver happens to land on the
            // cubic's float too, the two builds are indistinguishable at that x, and the
            // aggregate below reports how many samples actually discriminate.
            if (gotNear == solvedNear) ++nearMatchesSolver;
            if (solvedNear != cubic) ++nearDiscriminating;
        }
        std::printf("thirds branch: %d of 999 samples tell the branches apart; thirds pair matched "
                    "the cubic in %d, near-miss pair matched the solver in %d "
                    "(%d of those samples could have told the difference)\n",
                    discriminating, thirdsExact, nearMatchesSolver, nearDiscriminating);
        XC_MSG_ASSERT(nearDiscriminating > 100,
                      "the NEAR-MISS sweep cannot tell the solver from the fast path either "
                      "(%d samples differ) - the assertion below would be vacuous",
                      nearDiscriminating);
        XC_MSG_ASSERT(discriminating > 100,
                      "the sweep cannot tell the fast path from the solver (%d samples differ) - "
                      "the branch assertions below would be vacuous", discriminating);
        XC_MSG_ASSERT(thirdsExact == 999,
                      "a pair ON the thirds left the fast path in %d of 999 samples - it must take "
                      "it in all of them", 999 - thirdsExact);
        XC_MSG_ASSERT(nearMatchesSolver == 999,
                      "a pair 1e-6 OFF the thirds did not stay on the solver in %d of 999 samples",
                      999 - nearMatchesSolver);
    }

    // ---- THE PREPARED SEGMENT IS THE SHIPPED EVALUATION --------------------------
    //
    // `Easing::calculate` is a one-shot wrapper over util::EasingSegment, so the two must
    // agree BIT FOR BIT (not merely within a tolerance): a caller that prepares a segment
    // once and samples it 600 times must draw the curve playback plays.
    //
    // THIS GATE IS NOT AN ORACLE, and it must not be read as one: both sides are now the
    // same implementation. It is a guard against a SECOND evaluation path appearing in
    // `calculate` (the failure it catches is "someone re-spelled the maths"), plus a pin on
    // the branch flags, which a value comparison cannot see at all. The independent
    // evidence that the refactor changed nothing is the sweep ABOVE, which compares against
    // UnitBezier and against the direct cubic - code that is not the shipped path.
    {
        util::Easing::Param thirds;
        thirds.type = util::Easing::Type_Custom;
        thirds.cubicBezier = {1.0f / 3.0f, 0.2f, 2.0f / 3.0f, 0.8f};
        thirds.handleIn = util::Easing::HandleMode_Free;
        thirds.handleOut = util::Easing::HandleMode_Free;
        util::Easing::Param nearMiss = thirds;
        nearMiss.cubicBezier = {1.0f / 3.0f + 1e-6f, 0.2f, 2.0f / 3.0f - 1e-6f, 0.8f};
        util::Easing::Param derived = thirds;
        derived.handleOut = util::Easing::HandleMode_Auto;
        derived.handleIn = util::Easing::HandleMode_AutoClamped;
        util::Easing::AutoContext ctx;
        ctx.prev = {true, -1.0f, -0.2f};
        ctx.next = {true, 2.0f, 1.2f};
        ctx.inMode = util::Easing::HandleMode_AutoClamped;

        int storedExact = 0, nearExact = 0, derivedExact = 0;
        const float span = 1.0f;
        const util::EasingSegment preparedStored(thirds, span);
        const util::EasingSegment preparedNear(nearMiss, span);
        const util::EasingSegment preparedDerived(derived, ctx, span);
        for (int i = 0; i <= 1000; ++i) {
            const float t = float(i) / 1000.0f;
            const float viaSurface = util::Easing::calculate(thirds, t, 0.0f, 1.0f, span);
            const float viaPrepared = preparedStored.rate(t);
            if (viaSurface == viaPrepared) ++storedExact;
            if (util::Easing::calculate(nearMiss, t, 0.0f, 1.0f, span) == preparedNear.rate(t))
                ++nearExact;
            if (util::Easing::calculate(derived, ctx, t, 0.0f, 1.0f, span) == preparedDerived.rate(t))
                ++derivedExact;
        }
        std::printf("prepared segment: stored %d/1001, near-miss %d/1001, derived %d/1001 bitwise equal "
                    "to Easing::calculate; branch flags thirds=%d near=%d derived=%d\n",
                    storedExact, nearExact, derivedExact, int(preparedStored.usesThirdsPath()),
                    int(preparedNear.usesThirdsPath()), int(preparedDerived.usesThirdsPath()));
        XC_MSG_ASSERT(storedExact == 1001, "a prepared segment disagreed with Easing::calculate in %d of "
                      "1001 samples (stored pair)", 1001 - storedExact);
        XC_MSG_ASSERT(nearExact == 1001, "a prepared segment disagreed with Easing::calculate in %d of "
                      "1001 samples (near-thirds pair)", 1001 - nearExact);
        XC_MSG_ASSERT(derivedExact == 1001, "a prepared segment disagreed with Easing::calculate in %d of "
                      "1001 samples (derived pair)", 1001 - derivedExact);
        XC_MSG_ASSERT(preparedStored.usesThirdsPath() && !preparedNear.usesThirdsPath()
                      && preparedDerived.usesThirdsPath(),
                      "the prepared segment's branch flags do not match the predicate");
    }

    // ---- A ZERO SPAN IS THE LINEAR RATE, not a division by zero ----
    //
    // EasingSegment clamps a zero span to 1, and this is the one place the refactor changed an
    // answer: `t / 0` used to be +inf and UnitBezier::solve turned that into an endpoint-
    // gradient extrapolation (an unbounded number reaching a drawn curve). Nothing in the tree
    // asks for it - both funnels guard a zero-length segment before evaluating (sampleOver
    // returns the from value, getEasingRateFromTwoKeys returns the earlier key) - so this pins
    // the bounded answer a future caller gets instead of the old infinity.
    {
        util::Easing::Param custom;
        custom.type = util::Easing::Type_Custom;
        custom.cubicBezier = {1.0f / 3.0f, 0.2f, 2.0f / 3.0f, 0.8f};
        const float zeroSpan = util::Easing::calculate(custom, 0.5f, 0.0f, 1.0f, 0.0f);
        std::printf("zero span: calculate(custom, 0.5, 0, 1, 0) = %.4f (was an infinity)\n", zeroSpan);
        XC_MSG_ASSERT(std::fabs(zeroSpan - 0.5f) <= 1e-4f,
                      "a zero span produced %.4f, expected the linear 0.5", zeroSpan);
    }

    std::printf("easing_regression: PASS (%d curves, solver + weight + threads + auto + names "
                "+ defaults + thirds + crossed ok)\n",
                numCurves);
    return 0;
}
