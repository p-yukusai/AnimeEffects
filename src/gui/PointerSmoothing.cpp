#include "gui/PointerSmoothing.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace gui {

namespace {
// the lab's measured default: a 4-px staircase repeats every 4 px, and 12 px spans three periods,
// which is what makes the box cancel it outright
constexpr double kDefaultWindowPx = 12.0;
} // namespace

double PointerSmoothing::defaultWindowPx() {
    bool ok = false;
    const double value = qEnvironmentVariable("AE_POINTER_SMOOTH").toDouble(&ok);
    return ok ? std::max(0.0, value) : kDefaultWindowPx;
}

PointerSmoothing::PointerSmoothing(double aWindowPx): mWindowPx(std::max(0.0, aWindowPx)) {}

PointerSmoothing::PointerSmoothing(): PointerSmoothing(defaultWindowPx()) {}

void PointerSmoothing::setWindowPx(double aWindowPx) {
    mWindowPx = std::max(0.0, aWindowPx);
    if (!isEnabled())
        reset();
}

void PointerSmoothing::reset() {
    mInit = false;
    mPoints.clear();
    mLen = 0.0;
    mPrev = QPointF();
    mOut = QPointF();
}

void PointerSmoothing::begin(const QPointF& aPos) {
    reset();
    if (!isEnabled())
        return;
    mInit = true;
    mPrev = aPos;
    mOut = aPos;
    mPoints.push_back({aPos, 0.0});
}

QPointF PointerSmoothing::step(const QPointF& aPos) {
    if (!isEnabled()) {
        mOut = aPos;
        return mOut;
    }
    if (!mInit)
        begin(aPos);

    const double ds = std::hypot(aPos.x() - mPrev.x(), aPos.y() - mPrev.y());
    if (ds <= 0.0)
        return mOut;

    mPrev = aPos;
    mLen += ds;
    mPoints.push_back({aPos, mLen});
    // keep a margin: a query may still ask for the window behind the tip
    while (mPoints.size() > 2 && mPoints[1].s <= mLen - mWindowPx - 64.0)
        mPoints.pop_front();

    mOut = averageAt(mLen);
    return mOut;
}

QPointF PointerSmoothing::averageAt(double aEnd) const {
    if (mPoints.empty())
        return QPointF();
    const double start = std::max(aEnd - mWindowPx, mPoints.front().s);
    const double covered = aEnd - start;
    if (covered <= 0.0)
        return mPoints.back().p;

    QPointF integral;
    for (std::size_t i = 1; i < mPoints.size(); ++i) {
        const double a = std::max(mPoints[i - 1].s, start);
        const double b = std::min(mPoints[i].s, aEnd);
        if (b <= a)
            continue;
        const double len = mPoints[i].s - mPoints[i - 1].s;
        const double t0 = (a - mPoints[i - 1].s) / len;
        const double t1 = (b - mPoints[i - 1].s) / len;
        const QPointF p0 = mPoints[i - 1].p + (mPoints[i].p - mPoints[i - 1].p) * t0;
        const QPointF p1 = mPoints[i - 1].p + (mPoints[i].p - mPoints[i - 1].p) * t1;
        integral += (p0 + p1) * (0.5 * (b - a));
    }
    return integral / covered;
}

} // namespace gui
