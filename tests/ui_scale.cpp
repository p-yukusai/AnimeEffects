// THE UI SCALE'S VALUE, before it reaches Qt.
//
// gui::ui_scale is applied to QT_SCALE_FACTOR before QApplication exists, so a bad
// value here is a process that lays out at a scale the dialog never offered. The
// clamp and the environment string are therefore pure functions and are pinned
// here: 75..200, two decimals, so the 5% steps are exact.
#include <cstdio>
#include <QString>

#include "gui/UiScale.h"

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
} // namespace

int main() {
    using namespace gui::ui_scale;

    expect(clampPercent(50) == 75, "below the floor clamps to 75");
    expect(clampPercent(75) == 75, "the floor is kept");
    expect(clampPercent(100) == 100, "100 is kept");
    expect(clampPercent(125) == 125, "a step value is kept");
    expect(clampPercent(200) == 200, "the ceiling is kept");
    expect(clampPercent(250) == 200, "above the ceiling clamps to 200");
    expect(clampPercent(0) == 75, "zero clamps to the floor");
    expect(clampPercent(-10) == 75, "a negative value clamps to the floor");

    expect(environmentValue(75) == QStringLiteral("0.75"), "75% is 0.75");
    expect(environmentValue(100) == QStringLiteral("1.00"), "100% is 1.00");
    expect(environmentValue(125) == QStringLiteral("1.25"), "125% is 1.25");
    expect(environmentValue(200) == QStringLiteral("2.00"), "200% is 2.00");
    expect(environmentValue(33) == QStringLiteral("0.75"), "an out-of-range value is clamped first");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
