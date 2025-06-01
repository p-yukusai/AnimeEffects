#ifndef GUI_GUIRESOURCES_H
#define GUI_GUIRESOURCES_H

#include <QString>
#include <QIcon>
#include <QFileInfo>
#include <QPixmap>
#include <QHash>
#include <QSettings>
#include <QFont>
#include <QPainter>
#include <QPalette>
#include <QColor>
#include <QDirIterator>
#include <QStringList>
#include "XC.h"
#include "util/NonCopyable.h"
#include "util/Signaler.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QStyle>
#include <QStyleFactory>

namespace gui {

class GUIResources: private util::NonCopyable {
public:
    GUIResources(const QString& aResourceDir);
    ~GUIResources();

    QIcon icon(const QString& aName) const;

    QStringList themeList();
    bool hasTheme(const QString& aThemeId);
    void setTheme(const QString& aThemeId);

public:
    QPalette palette;
    void setPaletteDark(){
        palette.setColor(QPalette::Window, QColor(53,53,53));
        palette.setColor(QPalette::WindowText, Qt::white);
        palette.setColor(QPalette::Base, QColor(25,25,25));
        palette.setColor(QPalette::AlternateBase, QColor(53,53,53));
        palette.setColor(QPalette::ToolTipBase, Qt::white);
        palette.setColor(QPalette::ToolTipText, Qt::white);
        palette.setColor(QPalette::Text, Qt::white);
        palette.setColor(QPalette::Button, QColor(53,53,53));
        palette.setColor(QPalette::ButtonText, Qt::white);
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Link, QColor(42, 130, 218));
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, Qt::black);
    }
    void setPaletteDefault(){
        palette = QPalette();
    }
    static void setAppStyle() {
        #ifdef Q_OS_LINUX
            QApplication::setStyle(QStyleFactory::create("Fusion"));
        #elif  defined(Q_OS_APPLE)
            QApplication::setStyle(QStyleFactory::create("Fusion"));
        #else
            QApplication::setStyle(QStyleFactory::create("Fusion"));
        #endif


    }

    QString getThemeLocation() { return mTheme.path(); };
    QString getTheme() { return mTheme.id(); };

    // Font size methods
    void setFontSize(int size);
    int fontSize() const;
    void applyFontSize(); // Added helper to apply font size

    // signals
    util::Signaler<void(theme::Theme&)> onThemeChanged;
    void triggerOnThemeChanged();

    theme::Theme mTheme;

private:
    int mFontSize; // Added font size member

    typedef QHash<QString, QIcon*> IconMap;
    typedef QHash<QString, theme::Theme> ThemeMap;

    void loadIcons();
    void detectThemes();

    QString iconPath(const QString& aName);
    void loadIcon(const QString& aPath);

    QString mResourceDir;
    IconMap mIconMap;

    ThemeMap mThemeMap;
};

} // namespace gui

#endif // GUI_GUIRESOURCES_H
