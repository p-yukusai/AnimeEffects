#include <QSettings>
#include <QApplication>
#include "gui/MouseSetting.h"

namespace gui {

MouseSetting::MouseSetting(): invertZoom(), middleMouseMoveCanvas() {}

bool MouseSetting::operator==(const MouseSetting& aRhs) const {
    return invertZoom == aRhs.invertZoom && middleMouseMoveCanvas == aRhs.middleMouseMoveCanvas;
}

void MouseSetting::load() {
    QSettings settings(
        QSettings::IniFormat, QSettings::UserScope, QApplication::organizationName(), QApplication::applicationName()
    );
    settings.beginGroup("mousesettings");

    auto invZoom = settings.value("InvertZoom");
    auto invMVScale = settings.value("InvertMainViewScaling");
    auto mmMoveCanvas = settings.value("MiddleMouseMovesCanvas");

    // MIGRATION, ONCE: the wheel's meaning flipped. `false` is now the convention (wheel up zooms
    // in), which is what the old `InvertMainViewScaling = true` produced - so the old value maps to
    // its NEGATION and a hand-tuned file keeps the feel it had.
    if (invZoom.isValid())
        invertZoom = invZoom.toBool();
    else if (invMVScale.isValid())
        invertZoom = !invMVScale.toBool();
    else
        invertZoom = false;
    middleMouseMoveCanvas = mmMoveCanvas.isValid() ? mmMoveCanvas.toBool() : true;
}

void MouseSetting::save() {
    QSettings settings(
        QSettings::IniFormat, QSettings::UserScope, QApplication::organizationName(), QApplication::applicationName()
    );
    settings.beginGroup("mousesettings");

    settings.setValue("InvertZoom", invertZoom);
    // the superseded pair is removed, or a later load would migrate it again
    settings.remove("InvertMainViewScaling");
    settings.remove("InvertTimeLineScaling");
    settings.setValue("MiddleMouseMovesCanvas", middleMouseMoveCanvas);
}

} // namespace gui
