#ifndef GUI_UISCALE_H
#define GUI_UISCALE_H

#include <QString>

namespace gui {
namespace ui_scale {

// The application's UI scale, as a percentage.
//
// The line's QML and widget metrics are written in device-independent pixels, so the
// one mechanism that scales ALL of them - QML literals, QSS sizes, SVG icons and hit
// areas - is Qt's own device-pixel-ratio scale. Qt reads QT_SCALE_FACTOR before the
// QGuiApplication exists, which is why the value is applied in main() and why the
// setting says "needs restart": like the interface language, it is a property of the
// process, not a live edit.
constexpr int kDefaultPercent = 100;
constexpr int kMinPercent = 75;
constexpr int kMaxPercent = 200;

int clampPercent(int aPercent);

// The QT_SCALE_FACTOR value for a percentage: "1.25" for 125. Two decimals keep the
// common 5% steps exact in the environment string.
QString environmentValue(int aPercent);

// True when the user has chosen a scale (the key exists). Without a choice the
// process is left alone, so an externally set QT_SCALE_FACTOR still works.
bool hasStoredPercent();
int storedPercent();

// Apply the stored choice to QT_SCALE_FACTOR. MUST run before QApplication is
// constructed; after that Qt has already chosen its screen and DPR.
void applyToEnvironment();

} // namespace ui_scale
} // namespace gui

#endif // GUI_UISCALE_H
