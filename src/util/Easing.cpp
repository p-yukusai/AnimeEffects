#include "Easing.h"
#include <QtMath>
#include "util/EasingName.h"
#include "util/UnitBezier.h"

namespace {
// Picker display order, mirrored by the Easing::display* helpers. The enum
// ordinal is serialization order; this is the order pickers show (stepped
// holds first, then the analytic curves, then the bezier "Custom").
constexpr util::Easing::Type kEasingDisplayOrder[] = {
    util::Easing::Type_Floor, util::Easing::Type_Round, util::Easing::Type_Linear, util::Easing::Type_Sine,
    util::Easing::Type_Quad, util::Easing::Type_Cubic, util::Easing::Type_Quart, util::Easing::Type_Quint,
    util::Easing::Type_Expo, util::Easing::Type_Circ, util::Easing::Type_Back, util::Easing::Type_Elastic,
    util::Easing::Type_Bounce, util::Easing::Type_Custom,
};
constexpr int kEasingDisplayOrderCount = int(sizeof(kEasingDisplayOrder) / sizeof(kEasingDisplayOrder[0]));
} // namespace

namespace util {

//-------------------------------------------------------------------------------------------------
bool Easing::Param::isValidParam() const {
    if (type < 0 || Type_TERM <= type)
        return false;
    if (range < 0 || Range_TERM <= range)
        return false;
    if (weight < 0.0f || 1.0f < weight)
        return false;
    return true;
}

bool Easing::Param::operator==(const Param& aRhs) const {
    return type == aRhs.type && range == aRhs.range && weight == aRhs.weight &&
        cubicBezier == aRhs.cubicBezier;
}
//-------------------------------------------------------------------------------------------------
int Easing::displayIndexOf(const Type aType) {
    for (int i = 0; i < kEasingDisplayOrderCount; ++i) {
        if (kEasingDisplayOrder[i] == aType)
            return i;
    }
    return -1;
}

Easing::Type Easing::typeAtDisplayIndex(const int aIndex) {
    if (0 <= aIndex && aIndex < kEasingDisplayOrderCount)
        return kEasingDisplayOrder[aIndex];
    return Type_Linear;
}

int Easing::displayCount() { return kEasingDisplayOrderCount; }

//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
QString Easing::getTypeName(const Type aType) {
    switch (aType) {
    case Type_Floor:
        return EasingName::tr("Floor");
    case Type_Linear:
        return EasingName::tr("Linear");
    case Type_Sine:
        return EasingName::tr("Sine");
    case Type_Quad:
        return EasingName::tr("Quad");
    case Type_Cubic:
        return EasingName::tr("Cubic");
    case Type_Quart:
        return EasingName::tr("Quart");
    case Type_Quint:
        return EasingName::tr("Quint");
    case Type_Expo:
        return EasingName::tr("Expo");
    case Type_Circ:
        return EasingName::tr("Circ");
    case Type_Back:
        return EasingName::tr("Back");
    case Type_Elastic:
        return EasingName::tr("Elastic");
    case Type_Bounce:
        return EasingName::tr("Bounce");
    case Type_Custom:
        return EasingName::tr("Custom");
    case Type_Round:
        return EasingName::tr("Round");
    default:
        return "";
    }
}

QString Easing::getRangeName(const Range aRange) {
    switch (aRange) {
    case Range_In:
        return QString("In");
    case Range_Out:
        return QString("Out");
    case Range_InOut:
        return QString("All");
    default:
        return "";
    }
}

Easing::Type Easing::easingToEnum(QString easing) {
    QString aEasing;
    if (easing.isNull()) {
        QSettings settings;
        aEasing = settings.value("generalsettings/easing").toString();
    }
    else {
        aEasing = easing;
    }
    if (aEasing == "Floor" || aEasing == "None") // "None" is the legacy name
        return Type_Floor;
    if (aEasing == "Linear")
        return Type_Linear;
    if (aEasing == "Sine")
        return Type_Sine;
    if (aEasing == "Quad")
        return Type_Quad;
    if (aEasing == "Cubic")
        return Type_Cubic;
    if (aEasing == "Quart")
        return Type_Quart;
    if (aEasing == "Quint")
        return Type_Quint;
    if (aEasing == "Expo")
        return Type_Expo;
    if (aEasing == "Circ")
        return Type_Circ;
    if (aEasing == "Back")
        return Type_Back;
    if (aEasing == "Elastic")
        return Type_Elastic;
    if (aEasing == "Bounce")
        return Type_Bounce;
    if (aEasing == "Round")
        return Type_Round;
    if (aEasing == "Custom")
        return Type_Custom;
    return Type_Linear;
    // Default easing is Linear
}

Easing::Range Easing::rangeToEnum(QString range) {
    QString aRange;
    if (range.isNull()) {
        QSettings settings;
        aRange = settings.value("generalsettings/range").toString();
    } else {
        aRange = range;
    }
    if (aRange == "In")
        return Range_In;
    if (aRange == "Out")
        return Range_Out;
    if (aRange == "All")
        return Range_InOut;
    return Range_InOut;
    // Default range is InOut, defined as "All" by Hidefuku
}

//-------------------------------------------------------------------------------------------------
float Easing::calculate(
    const Type aType, const Range aRange, const float t, const float b, const float c, const float d) {
#define RETURN_BY_EASING_FUNCTION(func) \
    switch (aRange) { \
    case Range_In: \
        return func##In(t, b, c, d); \
    case Range_Out: \
        return func##Out(t, b, c, d); \
    case Range_InOut: \
        return func##InOut(t, b, c, d); \
    default: \
        return func##InOut(t, b, c, d); \
    }

    switch (aType) {
    case Type_Floor:
        return b;
    case Type_Round:
        // Stepped hold with a rounded frame: flip to the target value once
        // the normalized progress reaches the interval midpoint.
        return t / d < 0.5f ? b : b + c;
    case Type_Linear:
        return c * (t / d) + b;
    case Type_Sine:
        RETURN_BY_EASING_FUNCTION(sine);
    case Type_Quad:
        RETURN_BY_EASING_FUNCTION(quad);
    case Type_Cubic:
        RETURN_BY_EASING_FUNCTION(cubic);
    case Type_Quart:
        RETURN_BY_EASING_FUNCTION(quart);
    case Type_Quint:
        RETURN_BY_EASING_FUNCTION(quint);
    case Type_Expo:
        RETURN_BY_EASING_FUNCTION(expo);
    case Type_Circ:
        RETURN_BY_EASING_FUNCTION(circ);
    case Type_Back:
        RETURN_BY_EASING_FUNCTION(back);
    case Type_Elastic:
        RETURN_BY_EASING_FUNCTION(elastic);
    case Type_Bounce:
        RETURN_BY_EASING_FUNCTION(bounce);
    default:
        return b;
    }
#undef RETURN_BY_EASING_FUNCTION
}

float calculateBezier(const Easing::Param& aParam, const float t, const float b, const float c, const float d) {
    auto [x1, y1, x2, y2] = aParam.cubicBezier;
    // Normalized progress (frame offset / duration). This is the x fed to the
    // curve; it must NOT depend on weight.
    const float x = t / d;
    // The solver is built per evaluation: constructing a UnitBezier is 11 Horner
    // evals plus a handful of multiplies, and this keeps Easing stateless (no
    // cache, no shared mutable state on the frame path).
    const UnitBezier bezier(x1, y1, x2, y2);
    // The unit cubic solve returns the curve value; scale onto the key range.
    // Weight is deliberately NOT applied here: a custom bezier is exactly the
    // drawn curve, and its handles already span every shape weight could reach,
    // so the control is disabled in the UI (EasingItem::weightApplicable).
    // Keeping weight out of the math means a stale weight on a Custom key
    // (a value left over from another easing type, or one read from a saved
    // file) can never silently skew a drawn curve.
    return b + c * static_cast<float>(bezier.solve(x, UnitBezier::kBezierEpsilon));
}

float Easing::calculate(const Param& aParam, const float t, const float b, const float c, const float d) {
    // Param, relative frame, 0, 1, current frame, is bezier?
    if (aParam.type == Type_Custom) {
        return calculateBezier(aParam, t, b, c, d);
    }
    const float result = calculate(aParam.type, aParam.range, t, b, c, d);
    if (Type_Sine <= aParam.type && aParam.type < Type_Custom) {
        return result * aParam.weight + (c * (t / d) + b) * (1.0f - aParam.weight);
    }
    return result;
}

//-------------------------------------------------------------------------------------------------
float Easing::sineIn(const float t, const float b, const float c, const float d) { return -c * qCos(t / d * (M_PI / 2)) + c + b; }

float Easing::sineOut(const float t, const float b, const float c, const float d) { return c * qSin(t / d * (M_PI / 2)) + b; }

float Easing::sineInOut(const float t, const float b, const float c, const float d) { return -c / 2 * (qCos(M_PI * t / d) - 1) + b; }

//-------------------------------------------------------------------------------------------------
float Easing::quadIn(float t, const float b, const float c, const float d) {
    t /= d;
    return c * t * t + b;
}

float Easing::quadOut(float t, const float b, const float c, const float d) {
    t /= d;
    return -c * t * (t - 2) + b;
}

float Easing::quadInOut(float t, const float b, const float c, const float d) {
    t /= d / 2;
    if (t < 1) {
        return ((c / 2) * (t * t)) + b;
    } else {
        --t;
        return -c / 2 * ((t - 2) * t - 1) + b;
    }
}

//-------------------------------------------------------------------------------------------------
float Easing::cubicIn(float t, const float b, const float c, const float d) {
    t /= d;
    return c * t * t * t + b;
}

float Easing::cubicOut(float t, const float b, const float c, const float d) {
    t = t / d - 1;
    return c * (t * t * t + 1) + b;
}

float Easing::cubicInOut(float t, const float b, const float c, const float d) {
    t /= d / 2;
    if (t < 1) {
        return c / 2 * t * t * t + b;
    } else {
        t -= 2;
        return c / 2 * (t * t * t + 2) + b;
    }
}

//-------------------------------------------------------------------------------------------------
float Easing::quartIn(float t, const float b, const float c, const float d) {
    t /= d;
    return c * t * t * t * t + b;
}

float Easing::quartOut(float t, const float b, const float c, const float d) {
    t = t / d - 1;
    return -c * (t * t * t * t - 1) + b;
}

float Easing::quartInOut(float t, const float b, const float c, const float d) {
    t /= d / 2;
    if (t < 1) {
        return c / 2 * t * t * t * t + b;
    } else {
        t -= 2;
        return -c / 2 * (t * t * t * t - 2) + b;
    }
}

//-------------------------------------------------------------------------------------------------
float Easing::quintIn(float t, const float b, const float c, const float d) {
    t /= d;
    return c * t * t * t * t * t + b;
}

float Easing::quintOut(float t, const float b, const float c, const float d) {
    t = t / d - 1;
    return c * (t * t * t * t * t + 1) + b;
}

float Easing::quintInOut(float t, const float b, const float c, const float d) {
    t /= d / 2;
    if (t < 1) {
        return c / 2 * t * t * t * t * t + b;
    } else {
        t -= 2;
        return c / 2 * (t * t * t * t * t + 2) + b;
    }
}

//-------------------------------------------------------------------------------------------------
float Easing::expoIn(const float t, const float b, const float c, const float d) { return (t == 0) ? b : c * qPow(2, 10 * (t / d - 1)) + b; }

float Easing::expoOut(const float t, const float b, const float c, const float d) {
    return (t == d) ? b + c : c * (-qPow(2, -10 * t / d) + 1) + b;
}

float Easing::expoInOut(float t, const float b, const float c, const float d) {
    if (t == 0)
        return b;
    if (t == d)
        return b + c;

    if ((t /= d / 2) < 1) {
        return c / 2 * qPow(2, 10 * (t - 1)) + b;
    } else {
        return c / 2 * (-qPow(2, -10 * --t) + 2) + b;
    }
}

//-------------------------------------------------------------------------------------------------
float Easing::circIn(float t, const float b, const float c, const float d) {
    t /= d;
    return -c * (qSqrt(1 - t * t) - 1) + b;
}

float Easing::circOut(float t, const float b, const float c, const float d) {
    t = t / d - 1;
    return c * qSqrt(1 - t * t) + b;
}

float Easing::circInOut(float t, const float b, const float c, const float d) {
    t /= d / 2;
    if (t < 1) {
        return -c / 2 * (qSqrt(1 - t * t) - 1) + b;
    } else {
        t -= 2;
        return c / 2 * (qSqrt(1 - t * t) + 1) + b;
    }
}

//-------------------------------------------------------------------------------------------------
float Easing::backIn(float t, const float b, const float c, const float d) {
    float s = 1.70158f;
    t /= d;
    return c * t * t * ((s + 1) * t - s) + b;
}

float Easing::backOut(float t, const float b, const float c, const float d) {
    float s = 1.70158f;
    t = t / d - 1;
    return c * (t * t * ((s + 1) * t + s) + 1) + b;
}

float Easing::backInOut(float t, const float b, const float c, const float d) {
    float s = 1.70158f;
    t /= d / 2;
    if (t < 1) {
        s *= 1.525f;
        return c / 2 * (t * t * ((s + 1) * t - s)) + b;
    } else {
        t -= 2;
        s *= 1.525f;
        return c / 2 * (t * t * ((s + 1) * t + s) + 2) + b;
    }
}

//-------------------------------------------------------------------------------------------------
float Easing::elasticIn(float t, const float b, const float c, const float d) {
    if (t == 0)
        return b;
    if ((t /= d) == 1)
        return b + c;

    float p = d * 0.3f;
    float a = c;
    float s = p / 4;
    float postFix = a * qPow(2, 10 * (t -= 1));
    return -(postFix * qSin((t * d - s) * (2 * M_PI) / p)) + b;
}

float Easing::elasticOut(float t, const float b, const float c, const float d) {
    if (t == 0)
        return b;
    if ((t /= d) == 1)
        return b + c;

    float p = d * 0.3f;
    float a = c;
    float s = p / 4;
    return (a * qPow(2, -10 * t) * qSin((t * d - s) * (2 * M_PI) / p) + c + b);
}

float Easing::elasticInOut(float t, const float b, const float c, const float d) {
    if (t == 0)
        return b;
    if ((t /= d / 2) == 2)
        return b + c;

    float p = d * (0.3f * 1.5f);
    float a = c;
    float s = p / 4;

    if (t < 1) {
        float postFix = a * qPow(2, 10 * (t -= 1));
        return -0.5f * (postFix * qSin((t * d - s) * (2 * M_PI) / p)) + b;
    } else {
        float postFix = a * qPow(2, -10 * (t -= 1));
        return postFix * qSin((t * d - s) * (2 * M_PI) / p) * 0.5f + c + b;
    }
}

//-------------------------------------------------------------------------------------------------
float Easing::bounceIn(const float t, const float b, const float c, const float d) { return c - bounceOut(d - t, 0, c, d) + b; }

float Easing::bounceOut(float t, const float b, const float c, const float d) {
    if ((t /= d) < (1.0f / 2.75f)) {
        return c * (7.5625f * t * t) + b;
    } else if (t < (2.0f / 2.75f)) {
        float postFix = t -= (1.5f / 2.75f);
        return c * (7.5625f * (postFix)*t + 0.75f) + b;
    } else if (t < (2.5f / 2.75f)) {
        float postFix = t -= (2.25f / 2.75f);
        return c * (7.5625f * (postFix)*t + 0.9375f) + b;
    } else {
        float postFix = t -= (2.625f / 2.75f);
        return c * (7.5625f * (postFix)*t + 0.984375f) + b;
    }
}

float Easing::bounceInOut(const float t, const float b, const float c, const float d) {
    if (t < d / 2) {
        return bounceIn(t * 2, 0, c, d) * 0.5f + b;
    } else {
        return bounceOut(t * 2 - d, 0, c, d) * 0.5f + c * 0.5f + b;
    }
}

} // namespace util
