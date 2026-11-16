// THE CLIPBOARD FORM OF A CUSTOM EASING.
//
// The picker's Paste and the timeline's "Paste custom easing" both go through
// util::bezier_text::parse. The validation it owns is the difference between a
// typo and a broken key: "nan"/"inf" convert successfully with QString::toDouble,
// and an x outside [0,1] makes x(t) non-monotonic, which the solver inverts
// wrongly. y may overshoot by design.
#include <cmath>
#include <cstdio>
#include <QString>

#include "util/BezierText.h"

namespace {
int failures = 0;
int checks = 0;
void expect(bool aPassed, const char* aWhat) {
    ++checks;
    if (!aPassed) {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}
bool ok(const char* aText) {
    util::Easing::CubicBezier b{};
    return util::bezier_text::parse(QString::fromLatin1(aText), b);
}
} // namespace

int main() {
    util::Easing::CubicBezier b{};
    expect(util::bezier_text::parse(QStringLiteral("0.25,0.1,0.25,1"), b), "a standard ease is accepted");
    expect(b.x1 == 0.25f && b.y1 == 0.1f && b.x2 == 0.25f && b.y2 == 1.0f, "the four values land in order");
    expect(ok(" 0.1 , 0.2 , 0.3 , 0.4 "), "surrounding spaces are accepted");
    expect(ok("0,0,1,1"), "the endpoints are accepted");
    expect(ok("0,-2,1,3"), "y may overshoot in both directions");
    expect(ok("1,0,0,1"), "x at both ends is in the domain");

    expect(!ok("0,0,1"), "three fields are rejected");
    expect(!ok("0,0,1,1,0"), "five fields are rejected");
    expect(!ok("a,0,1,1"), "a non-number is rejected");
    expect(!ok("0,0,1,"), "an empty field is rejected");
    expect(!ok("nan,0,1,1"), "nan is rejected");
    expect(!ok("0,inf,1,1"), "inf is rejected");
    expect(!ok("0,0,1,nan"), "nan in y is rejected");
    expect(!ok("-0.1,0,1,1"), "x1 below the domain is rejected");
    expect(!ok("1.1,0,1,1"), "x1 above the domain is rejected");
    expect(!ok("0,0,-0.1,1"), "x2 below the domain is rejected");
    expect(!ok("0,0,1.1,1"), "x2 above the domain is rejected");

    const util::Easing::CubicBezier written{ 0.25f, 0.1f, 0.25f, 1.0f };
    expect(util::bezier_text::format(written) == QStringLiteral("0.25,0.1,0.25,1"), "format writes the paste form");
    util::Easing::CubicBezier round{};
    expect(util::bezier_text::parse(util::bezier_text::format(written), round), "the written form parses back");
    expect(round == written, "the round trip is exact");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
