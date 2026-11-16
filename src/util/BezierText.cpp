#include <cmath>
#include <QStringList>

#include "util/BezierText.h"

namespace util {
namespace bezier_text {

namespace {

bool inDomain(double aX) { return aX >= 0.0 && aX <= 1.0; }

} // namespace

bool parse(const QString& aText, Easing::CubicBezier& aBezier) {
    const QStringList fields = aText.split(',');
    if (fields.size() != 4) {
        return false;
    }
    double value[4] = { 0.0, 0.0, 0.0, 0.0 };
    for (int i = 0; i < 4; ++i) {
        bool ok = false;
        value[i] = fields[i].trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(value[i])) {
            return false;
        }
    }
    if (!inDomain(value[0]) || !inDomain(value[2])) {
        return false;
    }
    aBezier = { float(value[0]), float(value[1]), float(value[2]), float(value[3]) };
    return true;
}

QString format(const Easing::CubicBezier& aBezier) {
    return QStringLiteral("%1,%2,%3,%4")
        .arg(aBezier.x1)
        .arg(aBezier.y1)
        .arg(aBezier.x2)
        .arg(aBezier.y2);
}

} // namespace bezier_text
} // namespace util
