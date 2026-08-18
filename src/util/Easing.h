#ifndef UTIL_EASING
#define UTIL_EASING

#include <QString>
#include <QStringList>
#include <QSettings>

namespace util {

class Easing {
public:
    enum Type {
        Type_None,
        Type_Linear,
        Type_Sine,
        Type_Quad,
        Type_Cubic,
        Type_Quart,
        Type_Quint,
        Type_Expo,
        Type_Circ,
        Type_Back,
        Type_Elastic,
        Type_Bounce,
        Type_Custom,
        Type_TERM,
    };

    enum Range { Range_In, Range_Out, Range_InOut, Range_TERM};

    struct CubicBezier {
        float x1 = 0.0f;
        float y1 = 1.0f;
        float x2 = 1.0f;
        float y2 = 0.0f;
    };

    struct Param {
        Param()
            :
            type(Type_Linear), range(Range_InOut), weight(1.0f) {}
        bool isValidParam() const;
        bool operator==(const Param& aRhs) const;

        bool operator!=(const Param& aRhs) const { return !(*this == aRhs); }
        mutable Type type;
        Range range;
        float weight;
        mutable CubicBezier cubicBezier;
    };

    static QString getTypeName(Type aType);
    static QString getRangeName(Range aRange);
    static QStringList getTypeNameList();

    static float calculate(Type, Range, float t, float b, float c, float d);
    static float calculate(const Param&, float t, float b, float c, float d);

    static float sineIn(float t, float b, float c, float d);
    static float sineOut(float t, float b, float c, float d);
    static float sineInOut(float t, float b, float c, float d);

    static float quadIn(float t, float b, float c, float d);
    static float quadOut(float t, float b, float c, float d);
    static float quadInOut(float t, float b, float c, float d);

    static float cubicIn(float t, float b, float c, float d);
    static float cubicOut(float t, float b, float c, float d);
    static float cubicInOut(float t, float b, float c, float d);

    static float quartIn(float t, float b, float c, float d);
    static float quartOut(float t, float b, float c, float d);
    static float quartInOut(float t, float b, float c, float d);

    static float quintIn(float t, float b, float c, float d);
    static float quintOut(float t, float b, float c, float d);
    static float quintInOut(float t, float b, float c, float d);

    static float expoIn(float t, float b, float c, float d);
    static float expoOut(float t, float b, float c, float d);
    static float expoInOut(float t, float b, float c, float d);

    static float circIn(float t, float b, float c, float d);
    static float circOut(float t, float b, float c, float d);
    static float circInOut(float t, float b, float c, float d);

    static float backIn(float t, float b, float c, float d);
    static float backOut(float t, float b, float c, float d);
    static float backInOut(float t, float b, float c, float d);

    static float elasticIn(float t, float b, float c, float d);
    static float elasticOut(float t, float b, float c, float d);
    static float elasticInOut(float t, float b, float c, float d);

    static float bounceIn(float t, float b, float c, float d);
    static float bounceOut(float t, float b, float c, float d);
    static float bounceInOut(float t, float b, float c, float d);

    static Easing::Type easingToEnum(QString easing);
    static Easing::Range rangeToEnum(QString range);

private:
    Easing() {}
};

} // namespace util

#endif // UTIL_EASING
