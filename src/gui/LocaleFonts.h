#ifndef GUI_LOCALEFONTS_H
#define GUI_LOCALEFONTS_H

#include <QFont>
#include <QString>
#include <QStringList>

#include "gui/LocaleParam.h"

namespace gui {
namespace locale_fonts {

// The interface font choice, as pure functions of the language, so the policy is
// testable without a LocaleDecider (which reads QSettings and QLocale).
//
// The CJK collection ships one face per region: JP for Japanese, KR for Korean,
// SC for Simplified Chinese. A Latin UI keeps Geist and carries the JP face as a
// fallback, which covers Han and Hangul for layer names, text layers and file names.
QString cjkFamilyForLanguage(const QString& aLanguageAbbrev);
QString defaultFamilyForLanguage(const QString& aLanguageAbbrev);
bool isCjkFamily(const QString& aFamily);

// The fallback list for a primary family: empty for a CJK face (it already covers
// Latin and its region's glyphs), otherwise the JP face.
QStringList fallbackFamiliesFor(const QString& aPrimary);

// The QFont for a locale: families first, then the locale's size ("8pt"/"10pt"/
// "12px"/a bare number). Starts from the application font so the platform's style
// hints survive.
QFont fontFor(const LocaleParam& aLocale);

} // namespace locale_fonts
} // namespace gui

#endif // GUI_LOCALEFONTS_H
