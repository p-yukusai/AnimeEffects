#include <QSettings>
#include <QVariant>

#include "gui/UiScale.h"

namespace gui {
namespace ui_scale {

namespace {

const char* const kKey = "generalsettings/ui/uiScale";

} // namespace

int clampPercent(int aPercent) {
    if (aPercent < kMinPercent)
        return kMinPercent;
    if (aPercent > kMaxPercent)
        return kMaxPercent;
    return aPercent;
}

QString environmentValue(int aPercent) {
    return QString::number(clampPercent(aPercent) / 100.0, 'f', 2);
}

bool hasStoredPercent() {
    QSettings settings;
    return settings.value(QLatin1String(kKey)).isValid();
}

int storedPercent() {
    QSettings settings;
    const QVariant value = settings.value(QLatin1String(kKey));
    return value.isValid() ? clampPercent(value.toInt()) : kDefaultPercent;
}

void applyToEnvironment() {
    if (!hasStoredPercent())
        return;
    qputenv("QT_SCALE_FACTOR", environmentValue(storedPercent()).toLatin1());
}

} // namespace ui_scale
} // namespace gui
