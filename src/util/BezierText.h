#ifndef UTIL_BEZIERTEXT_H
#define UTIL_BEZIERTEXT_H

#include <QString>

#include "util/Easing.h"

namespace util {
namespace bezier_text {

// Parse the clipboard form of a cubic bezier: four comma-separated numbers,
// "x1,y1,x2,y2".
//
// x is the curve's DOMAIN and must stay in [0,1]: outside it x(t) is no longer
// monotonic, and the solver inverts it wrongly (a key that runs forwards then
// backwards). y is the value axis and may overshoot - an overshooting ease is a
// legitimate bezier, not an error. "nan" and "inf" parse successfully with
// QString::toDouble, so finiteness is checked explicitly; a non-finite control
// point reaches the solver as NaN.
bool parse(const QString& aText, Easing::CubicBezier& aBezier);

// The write side, in the same form (used by the picker's Copy).
QString format(const Easing::CubicBezier& aBezier);

} // namespace bezier_text
} // namespace util

#endif // UTIL_BEZIERTEXT_H
