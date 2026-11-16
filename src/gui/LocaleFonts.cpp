#include <QGuiApplication>

#include "gui/LocaleFonts.h"

namespace gui {
namespace locale_fonts {

namespace {

const char* const kJapaneseFamily = "Noto Sans CJK JP";
const char* const kKoreanFamily = "Noto Sans CJK KR";
const char* const kSimplifiedChineseFamily = "Noto Sans CJK SC";
const char* const kLatinFamily = "Geist";

} // namespace

QString cjkFamilyForLanguage(const QString& aLanguageAbbrev) {
    if (aLanguageAbbrev == QLatin1String("ja"))
        return QString::fromLatin1(kJapaneseFamily);
    if (aLanguageAbbrev == QLatin1String("ko"))
        return QString::fromLatin1(kKoreanFamily);
    if (aLanguageAbbrev == QLatin1String("zh"))
        return QString::fromLatin1(kSimplifiedChineseFamily);
    return QString();
}

QString defaultFamilyForLanguage(const QString& aLanguageAbbrev) {
    const QString cjk = cjkFamilyForLanguage(aLanguageAbbrev);
    return cjk.isEmpty() ? QString::fromLatin1(kLatinFamily) : cjk;
}

bool isCjkFamily(const QString& aFamily) {
    return aFamily.startsWith(QLatin1String("Noto Sans CJK ")) ||
           aFamily.startsWith(QLatin1String("Noto Serif CJK "));
}

QStringList fallbackFamiliesFor(const QString& aPrimary) {
    if (aPrimary.isEmpty() || isCjkFamily(aPrimary))
        return {};
    return { QString::fromLatin1(kJapaneseFamily) };
}

QFont fontFor(const LocaleParam& aLocale) {
    QFont font = QGuiApplication::font();
    const QStringList families = aLocale.fontFamilies();
    if (!families.isEmpty())
        font.setFamilies(families);

    const QString size = aLocale.fontSize.trimmed();
    bool ok = false;
    if (size.endsWith(QLatin1String("pt"), Qt::CaseInsensitive)) {
        const qreal pt = size.left(size.size() - 2).toDouble(&ok);
        if (ok)
            font.setPointSizeF(pt);
    } else if (size.endsWith(QLatin1String("px"), Qt::CaseInsensitive)) {
        const int px = size.left(size.size() - 2).toInt(&ok);
        if (ok)
            font.setPixelSize(px);
    } else if (!size.isEmpty()) {
        const qreal pt = size.toDouble(&ok);
        if (ok)
            font.setPointSizeF(pt);
    }
    return font;
}

} // namespace locale_fonts
} // namespace gui
