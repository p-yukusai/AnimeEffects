#include "gui/GUIResources.h"
#include "gui/ScrollBarStyle.h"
#include <QApplication>
#include <QStyleFactory>
#include <algorithm>

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

QColor GUIResources::viewportBackground() const {
    if (mTheme.isDefault()) {
        return QColor(216, 216, 216); // light theme
    }
    return QColor(31, 31, 31); // dark themes: the viewport sinks below the #262626 floor
}

QString GUIResources::dialogButtonStyleSheet() const {
    // Flat, modern styling for dialog buttons. This is applied application-wide so
    // that parentless dialogs (e.g. QMessageBox) inherit it as well, matching the
    // per-widget QPushButton styling defined in each theme's standard.ssa.
    if (mTheme.id().contains("dark")) {
        return QStringLiteral(
            "QPushButton {"
            "    color: #f0f0f0;"
            "    background-color: #3d3d3d;"
            "    border: 1px solid #565656;"
            "    border-radius: 4px;"
            "    padding: 4px 12px;"
            "    outline: none;"
            "}"
            "QPushButton:hover { background-color: #484848; border-color: #646464; }"
            "QPushButton:pressed { background-color: #303030; }"
            "QPushButton:checked { background-color: #303030; }"
            "QPushButton:disabled { color: #707070; background-color: #2c2c2c; border-color: #3e3e3e; }"
            "QPushButton:focus { border-color: #808080; }"
        );
    }
    return QStringLiteral(
        "QPushButton {"
        "    color: #202020;"
        "    background-color: #f2f2f2;"
        "    border: 1px solid #b8b8b8;"
        "    border-radius: 4px;"
        "    padding: 4px 12px;"
        "    outline: none;"
        "}"
        "QPushButton:hover { background-color: #e6e6e6; border-color: #a8a8a8; }"
        "QPushButton:pressed { background-color: #d8d8d8; }"
        "QPushButton:checked { background-color: #d8d8d8; }"
        "QPushButton:disabled { color: #9a9a9a; background-color: #f6f6f6; border-color: #d0d0d0; }"
        "QPushButton:focus { border-color: #909090; }"
    );
}

void GUIResources::loadIcon(const QString& aPath) {
    QString name = QFileInfo(aPath).baseName();

    // Theme icon dirs are generated at build time (tools/icon_tint), so the
    // images are already colored for the theme. QIcon(aPath) keeps vector
    // sources (SVG) scalable instead of pinning them to a raster size.
    QIcon* icon = new QIcon(aPath);
    mIconMap[name] = icon;

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

    const QString iconDirPath(mResourceDir + "/themes/" + mTheme.id() + "/icon");

    // Legacy icons are raster, the phosphor/at-icons ones are per-theme SVG
    // (both generated at build time by tools/icon_tint).
    QStringList filters;
    filters << "*.png" << "*.svg";
    QDirIterator itr(iconDirPath, filters, QDir::Files, QDirIterator::NoIteratorFlags);

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
    setAppStyle();
    const bool themeChanged = (mTheme.id() != aThemeId && hasTheme(aThemeId));
    if (themeChanged) {
        mTheme = mThemeMap.value(aThemeId);
        loadIcons();
    }
    setPaletteDefault();
    if (mTheme.id().contains("dark")) {
        setPaletteDark();
    }
    QApplication::setPalette(palette);
    qApp->setStyleSheet(dialogButtonStyleSheet());
    if (mTheme.id().contains("dark")) {
        // Scrollbar handles are drawn by the proxy style (a deterministic
        // 4px pill) instead of the QSS border-image path, which distorts
        // small pills. Wrap the current style (QStyleSheetStyle) so the QSS
        // still drives everything else; light themes keep their own styling.
        qApp->setStyle(new ScrollBarStyle(qApp->style()));
    }
    // Notify after the new palette is live so palette-reading handlers
    // (timeline theme, object-tree item colors) observe the new theme.
    if (themeChanged) {
        onThemeChanged(mTheme);
    }
}

void GUIResources::triggerOnThemeChanged() { onThemeChanged(mTheme); }

} // namespace gui
