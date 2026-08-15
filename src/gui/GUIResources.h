#ifndef GUI_GUIRESOURCES_H
#define GUI_GUIRESOURCES_H

#include <QHash>
#include <QIcon>
#include <QPalette>
#include <QString>
#include <QStringList>

#include "XC.h"
#include "util/NonCopyable.h"
#include "util/Signaler.h"
#include "theme/Colors.h"
#include "theme/Theme.h"

namespace gui {

class GUIResources: private util::NonCopyable {
public:
    GUIResources(const QString& aResourceDir);
    ~GUIResources() = default;

    QIcon icon(const QString& aName) const;
    // The same glyph in the active (max-contrast) color as the only state;
    // button widgets swap to it while hovered.
    QIcon iconActive(const QString& aName) const;
    QColor viewportBackground() const;

    QStringList themeList();
    void setTheme(const QString& aThemeId);
    void setAccentHue(double aHue);
    double accentHue() const { return mHue; }

    // signals
    util::Signaler<void(theme::Theme&)> onThemeChanged;
    void triggerOnThemeChanged();

    theme::Theme mTheme;

private:
    typedef QHash<QString, QIcon> IconMap;

    static void setAppStyle();

    // Recompute the palette, re-tint icons, and re-apply palette + QSS.
    void applyAppearance();
    void loadIcons();
    void applyPalette(const theme::Colors& aColors);
    QString dialogButtonStyleSheet() const;

    QString mResourceDir;
    QString mIconDir;  // runtime-tinted scratch dir
    double mHue;
    IconMap mIconMap;
    QPalette palette;
};

} // namespace gui

#endif // GUI_GUIRESOURCES_H
