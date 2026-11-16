#include <QLocale>
#include <QSettings>
#include "gui/LocaleDecider.h"
#include "gui/LocaleFonts.h"

namespace gui {

LocaleDecider::LocaleDecider(): mLocaleParam(), mTranslator(), mHasTranslator() {
    QString locAbb;

    {
        QSettings settings;
        auto langVar = settings.value("generalsettings/language");
        auto language = langVar.isValid() ? langVar.toString() : QString();

        if (language == "English") {
            // Load the English catalog even though English is the source language:
            // it is where Qt's NUMERUS forms live ("%n key(s)" -> "1 key"/"2 keys",
            // "%n channel(s)" -> "1 channel"/"3 channels"). Without it the source
            // string's developer shorthand is what the user sees.
            locAbb = "en";
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
            if (language == QLocale::English) {
                locAbb = "en";
            }
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

    // Locale font defaults: a CJK interface uses the shipped face for its region
    // (JP/KR/SC); a Latin one keeps Geist and carries the JP face as a fallback, so
    // a Japanese layer name still renders without a system CJK font. The size is
    // per-locale (zh uses a larger face). Both are stored in QSettings so they stay
    // tunable without a shipped file; these are the shipped defaults.
    {
        const QString sizeDefault = (locAbb == QLatin1String("zh")) ? QStringLiteral("10pt") : QStringLiteral("8pt");
        const QString localeGroup = locAbb.isEmpty() ? QStringLiteral("default") : locAbb;
        const QString defaultFamily = locale_fonts::defaultFamilyForLanguage(locAbb);

        QSettings settings;
        // The interface-font setting is the user's explicit choice and wins over
        // the locale group.
        const QString customFamily = settings.value("generalsettings/ui/fontFamily").toString().trimmed();
        QString family;
        {
            settings.beginGroup("locale");
            settings.beginGroup(localeGroup);
            family = settings.value("fontFamily", defaultFamily).toString().trimmed();
            mLocaleParam.fontSize = settings.value("fontSize", sizeDefault).toString();
        }
        if (!customFamily.isEmpty())
            family = customFamily;
        if (family.isEmpty())
            family = defaultFamily;

        mLocaleParam.fontFamily = family;
        mLocaleParam.fallbackFamilies = locale_fonts::fallbackFamiliesFor(family);
    }
}

} // namespace gui
