#ifndef GUI_LOCALEPARAM_H
#define GUI_LOCALEPARAM_H

#include <QFont>
#include <QString>
#include <QStringList>

namespace gui {

class LocaleParam {
public:
    LocaleParam(): fontFamily(), fallbackFamilies(), fontSize() {}

    // The face the interface asks for first (a QSS font-family can name only one).
    QString fontFamily;
    // Faces Qt should fall back to for glyphs the primary lacks. On a Geist UI this
    // is the shipped CJK face, so a Japanese layer name renders on every platform
    // instead of depending on a system CJK font being installed.
    QStringList fallbackFamilies;
    QString fontSize;

    // primary + fallbacks, the form QFont::setFamilies takes.
    QStringList fontFamilies() const {
        QStringList families;
        if (!fontFamily.isEmpty())
            families.push_back(fontFamily);
        for (const QString& family : fallbackFamilies) {
            if (!family.isEmpty() && !families.contains(family))
                families.push_back(family);
        }
        return families;
    }
};

} // namespace gui

#endif // GUI_LOCALEPARAM_H
