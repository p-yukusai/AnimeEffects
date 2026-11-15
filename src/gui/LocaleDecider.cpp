#include <QLocale>
#include <QSettings>
#include "gui/LocaleDecider.h"

namespace gui {

LocaleDecider::LocaleDecider(): mLocaleParam(), mTranslator(), mHasTranslator() {
    QString locAbb;

    {
        QSettings settings;
        auto langVar = settings.value("generalsettings/language");
        auto language = langVar.isValid() ? langVar.toString() : QString();

        if (language == "English") {
        } else if (language == "Japanese") {
            locAbb = "ja";
        } else if (language == "Chinese") {
            locAbb = "zh";
        }
        else if (language == "Spanish") {
            locAbb = "es";
        }
        else if (language == "Russian") {
            locAbb = "ru";
        }
        else if (language == "Korean") {
            locAbb = "ko";
        }
        else {
            auto language = QLocale::system().language();
            if (language == QLocale::Japanese) {
                locAbb = "ja";
            }
            if (language == QLocale::Chinese) {
                locAbb = "zh";
            }
            if (language == QLocale::Spanish) {
                locAbb = "es";
            }
            if (language == QLocale::Russian) {
                locAbb = "ru";
            }
            if (language == QLocale::Korean) {
                locAbb = "ko";
            }
        }
    }

    if (!locAbb.isEmpty()) {
        if (mTranslator.load("translation_" + locAbb, ":/i18n")) {
            mHasTranslator = true;
        }
    }

    // Locale font defaults: the bundled Geist family everywhere; the size is
    // per-locale (zh uses a larger face for CJK). Stored in QSettings so they
    // are tunable without a shipped file; these are the shipped defaults.
    {
        const QString sizeDefault = (locAbb == QLatin1String("zh")) ? QStringLiteral("10pt") : QStringLiteral("8pt");
        QSettings settings;
        settings.beginGroup("locale");
        settings.beginGroup(locAbb.isEmpty() ? QStringLiteral("default") : locAbb);
        mLocaleParam.fontFamily = settings.value("fontFamily", QStringLiteral("Geist")).toString();
        mLocaleParam.fontSize = settings.value("fontSize", sizeDefault).toString();
    }
}

} // namespace gui
