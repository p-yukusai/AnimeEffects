#include "gui/theme/Theme.h"

#include <QFile>
#include <QGuiApplication>
#include <QStyleHints>
#include <QSettings>
#include <QTextStream>

#include "gui/theme/Colors.h"
#include "gui/theme/Icons.h"

namespace theme {

Theme::Theme(QString aResourceDir, QString aId): mId(aId), mResourceDir(aResourceDir) {}

QString Theme::id() const { return mId; }

bool Theme::isDark() const {
    // "system" follows the OS color scheme; light/dark are explicit.
    if (mId == "light") return false;
    if (mId == "dark") return true;
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QString Theme::loadStylesheet(const QString& aName) const {
    // The templates are shared (one file per stylesheet, not per theme); the
    // token values live in theme::Colors, so the same text serves every theme.
    QFile file(mResourceDir + "/stylesheet/" + aName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return Colors::current().substitute(QTextStream(&file).readAll(), iconDir());
}

} // namespace theme
