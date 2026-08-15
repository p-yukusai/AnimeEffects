#ifndef GUI_THEME_INFO_H
#define GUI_THEME_INFO_H

#include <QString>

namespace theme {

// The active appearance identity (light/dark) plus the template loader. The
// derived color palette lives in theme::Colors (cached); this only carries the
// id and knows where the shared .ssa templates live.
class Theme {
public:
    Theme(QString aResourceDir, QString aId);

    QString id() const;
    bool isDark() const;

    // Loads the shared stylesheet template and fills its @token@/ @icondir@
    // placeholders with the active palette. Empty if the template is missing.
    QString loadStylesheet(const QString& aName) const;

private:
    QString mId;
    QString mResourceDir;
};

} // namespace theme

#endif // GUI_THEME_INFO_H
