#include <QFile>
#include <QLocale>
#include <QTextStream>
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
        } else {
            auto language = QLocale::system().language();
            if (language == QLocale::Japanese) {
                locAbb = "ja";
            }
            if (language == QLocale::Chinese) {
                locAbb = "zh";
            }
        }
    }

    if (!locAbb.isEmpty()) {
        if (mTranslator.load("translation_" + locAbb, "data/locale")) {
            mHasTranslator = true;
        }
    }

    {
#if defined(Q_OS_WIN)
        const QString opt = "_win";
#elif defined(Q_OS_MAC)
        const QString opt = "_mac";
#else
        const QString opt = "";
#endif

        const QString preference = locAbb.isEmpty() ? "preference" : "preference_" + locAbb;

        QFile file("./data/locale/" + preference + ".txt");
        if (file.open(QIODevice::ReadOnly)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                auto kv = in.readLine().split('=');
                if (kv.count() != 2)
                    continue;
                auto key = kv[0].trimmed();
                auto value = kv[1].trimmed();

                if (key == "font_family" + opt) {
                    mLocaleParam.fontFamily = value;
                } else if (key == "font_size" + opt) {
                    mLocaleParam.fontSize = value;
                }
            }
        }
    }
}

} // namespace gui
