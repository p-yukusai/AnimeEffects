#include "gui/GUIResources.h"
#include <QApplication>
#include <QStyleFactory>

namespace gui {

GUIResources::GUIResources(const QString& aResourceDir):
    mResourceDir(aResourceDir), mIconMap(), mThemeMap(), mTheme(aResourceDir) {
    detectThemes();

    QSettings settings;
    auto theme = settings.value("generalsettings/ui/theme");
    if (!theme.isValid()) {
        settings.setValue("generalsettings/ui/theme", "breeze_dark");
        theme = "breeze_dark";
    }
    settings.sync();
    setTheme(theme.toString());

    // Initialize font size
    bool ok;
    int savedFontSize = settings.value("generalsettings/ui/fontsize", QApplication::font().pointSize()).toInt(&ok);
    if (!ok || savedFontSize <= 0) { // Basic validation for font size
        mFontSize = QApplication::font().pointSize();
        if (mFontSize <= 0) { // If system default is also invalid, use a hardcoded default
            mFontSize = 12;
        }
    } else {
        mFontSize = savedFontSize;
    }
    applyFontSize(); // Apply initial font size

    loadIcons();
}

GUIResources::~GUIResources() {
    for (IconMap::iterator itr = mIconMap.begin(); itr != mIconMap.end(); ++itr) {
        QIcon* icon = *itr;
        delete icon;
    }
}

QIcon GUIResources::icon(const QString& aName) const {
    QIcon* icon = mIconMap[aName];
    if (icon) {
        return *icon;
    } else {
        // I don't see a benefit to asserting zero just because an icon is missing...
        // XC_ASSERT(0);
        return {};
    }
}

QString GUIResources::iconPath(const QString& aName) {
    bool loadLightIcons = mTheme.isDefault() && mTheme.isDark();
    return mTheme.path() + "/icon" + (loadLightIcons ? "/light" : "") + "/" + aName + ".png";
}

void GUIResources::loadIcon(const QString& aPath) {
    QString name = QFileInfo(aPath).baseName();
    QPixmap source(aPath);

    QIcon* icon = new QIcon();
    mIconMap[name] = icon;
    icon->addPixmap(source, QIcon::Normal, QIcon::Off);

#if 0
    {
        QPixmap work = source;
        QPainter painter(&work);
        //painter.setCompositionMode(QPainter::CompositionMode_Multiply);
        painter.setCompositionMode(QPainter::CompositionMode_Screen);
        painter.fillRect(work.rect(), QColor(128, 128, 128, 128));
        painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        painter.drawPixmap(source.rect(), source);
        painter.end();
        //icon->addPixmap(work, QIcon::Selected, QIcon::On);
        icon->addPixmap(work, QIcon::Disabled, QIcon::Off);
    }
#endif
}

void GUIResources::loadIcons() {
    if (!mIconMap.empty()) {
        QHashIterator<QString, QIcon*> i(mIconMap);
        while (i.hasNext()) {
            i.next();
            QIcon* icon = i.value();
            // qDebug() << i.key() << ": " << i.value();
            delete icon;
        }
        mIconMap.clear();
    }

    bool loadLightIcons = mTheme.isDefault() && mTheme.isDark();
    const QString iconDirPath(mResourceDir + "/themes/" + mTheme.id() + "/icon" + (loadLightIcons ? "/light" : ""));

    QStringList filters;
    filters << "*.png";
    QDirIterator itr(iconDirPath, filters, QDir::Files, QDirIterator::Subdirectories);

    while (itr.hasNext()) {
        loadIcon(itr.next());
    }
}

void GUIResources::detectThemes() {
    const QString themesDirPath(mResourceDir + "/themes");

    QDirIterator itr(themesDirPath, QDir::Dirs, QDirIterator::FollowSymlinks);

    while (itr.hasNext()) {
        itr.next();
        if (itr.fileName() != "." && itr.fileName() != "..") {
            qDebug() << Q_FUNC_INFO << itr.fileName();
            theme::Theme theme(mResourceDir, itr.fileName());
            mThemeMap.insert(itr.fileName(), theme);
        }
    }
}

QStringList GUIResources::themeList() {
    QStringList kThemeList;
    if (!mThemeMap.empty()) {
        QHashIterator<QString, theme::Theme> i(mThemeMap);
        while (i.hasNext()) {
            i.next();
            kThemeList.append(i.key());
        }
    }
    return kThemeList;
}

bool GUIResources::hasTheme(const QString& aThemeId) { return mThemeMap.contains(aThemeId); }

void GUIResources::setTheme(const QString& aThemeId) {
    setAppStyle(); // Applies style first
    if (mTheme.id() != aThemeId && hasTheme(aThemeId)) {
        mTheme = mThemeMap.value(aThemeId);
        loadIcons();
        onThemeChanged(mTheme);
    }
    setPaletteDefault();
    if (mTheme.id().contains("dark")){
        setPaletteDark();
    }
    QApplication::setPalette(palette);
    applyFontSize(); // Apply font size after theme changes
}

void GUIResources::triggerOnThemeChanged() { onThemeChanged(mTheme); }

// Font size methods implementation
void GUIResources::setFontSize(int size) {
    if (size <= 0) return; // Basic validation
    mFontSize = size;
    QSettings settings;
    settings.setValue("generalsettings/ui/fontsize", mFontSize);
    settings.sync();
    applyFontSize();
}

int GUIResources::fontSize() const {
    return mFontSize;
}

void GUIResources::applyFontSize() {
    QFont currentFont = QApplication::font();
    currentFont.setPointSize(mFontSize);
    QApplication::setFont(currentFont);
}

} // namespace gui
