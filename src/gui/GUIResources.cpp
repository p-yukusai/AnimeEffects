#include "gui/GUIResources.h"

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QStyleHints>

#include "AudioPlaybackWidget.h"
#include "gui/AppStyle.h"
#include "theme/Icons.h"

namespace gui {

GUIResources::GUIResources(const QString& aResourceDir):
    mResourceDir(aResourceDir),
    mIconDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/icons"),
    mAccent(theme::kDefaultAccent),
    mTheme(aResourceDir, "light") {
    setAppStyle();

    QSettings settings;
    QString themeId = settings.value("generalsettings/ui/theme", "dark").toString();
    // Migrate legacy theme ids (default/breeze_dark) to light/dark, then fall
    // back to a known theme when the saved id is missing.
    if (themeId == "default") themeId = "light";
    else if (themeId == "breeze_dark") themeId = "dark";
    else if (themeId == "breeze_dark_high_dpi") themeId = "dark";
    else if (themeId == "classic") themeId = "light";
    if (themeId != "system" && themeId != "light" && themeId != "dark") themeId = "dark";
    settings.setValue("generalsettings/ui/theme", themeId);

    // Accent: the selected Tailwind row, persisted by name.
    const QString accentName = settings.value("generalsettings/ui/accent", QString()).toString();
    mAccent = theme::accentFromName(accentName);
    settings.setValue("generalsettings/ui/accent", QLatin1String(theme::accentName(mAccent)));

    mTheme = theme::Theme(mResourceDir, themeId);
    applyAppearance();

    // "system" follows the OS color scheme; follow a live light/dark switch.
    mSchemeConnection = QObject::connect(
        QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
        [this](Qt::ColorScheme) {
            if (mTheme.id() == "system") {
                applyAppearance();
                onThemeChanged(mTheme);
            }
        });
}

GUIResources::~GUIResources() {
    QObject::disconnect(mSchemeConnection);
}

QIcon GUIResources::icon(const QString& aName) const {
    // Missing icons resolve to an empty QIcon; callers tolerate it.
    return mIconMap.value(aName);
}

QIcon GUIResources::iconActive(const QString& aName) const {
    // The active-only variant (hover swaps to it). The base map stores the
    // rest/active pair; this one is the active glyph as the resting state so
    // an unchecked hovered button renders it. Checked buttons keep rendering
    // the On pixmap regardless.
    return mIconMap.value(aName + "-active");
}

QColor GUIResources::viewportBackground() {
    // the canvas sinks below the base floor (the recessed token)
    return theme::Colors::current().recessed;
}

QStringList GUIResources::themeList() {
    return QStringList() << "system" << "light" << "dark";
}

void GUIResources::setTheme(const QString& aThemeId) {
    if (mTheme.id() == aThemeId) return;
    mTheme = theme::Theme(mResourceDir, aThemeId);
    applyAppearance();
    onThemeChanged(mTheme);
}

void GUIResources::setAccent(const theme::AccentColor aAccent) {
    if (mAccent == aAccent) return;
    mAccent = aAccent;
    QSettings().setValue("generalsettings/ui/accent", QLatin1String(theme::accentName(aAccent)));
    applyAppearance();
    onThemeChanged(mTheme);
}

void GUIResources::triggerOnThemeChanged() { onThemeChanged(mTheme); }

void GUIResources::setAppStyle() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
}

void GUIResources::applyAppearance() {
    theme::Colors::activate(mTheme.isDark(), mAccent);
    const theme::Colors& c = theme::Colors::current();

    // The canonical currentColor set is tinted at runtime into the scratch
    // dir; QSS and C++ both read from it (theme::iconDir).
    theme::tintIcons(mResourceDir + "/icons", mIconDir, c);
    theme::setIconDir(mIconDir);
    loadIcons();

    applyPalette(c);
    QApplication::setPalette(palette);
    // The app proxy style custom-paints the primitives QSS can't do well:
    // scrollbar pill, popup panels/rows, and the tree-branch carets (QSS
    // ::branch has no sizing knob). Install it BEFORE setStyleSheet: at that
    // point the app style is Fusion, which setStyle reparents under the
    // proxy. Installing after would capture the QStyleSheetStyle wrapper that
    // setStyleSheet just created; QApplication::setStyle then derefs that
    // wrapper (refcount 1 -> 0) and deletes it, leaving the proxy's base
    // dangling — a use-after-free on every forwarded call. Install it once:
    // setStyle slots the passed style under the stylesheet wrapper
    // (QStyleSheetStyle), so qobject_cast<AppStyle*>(qApp->style()) never
    // matches and the old guard wrapped a fresh proxy on every appearance
    // change.
    if (!mAppStyleInstalled) {
        qApp->setStyle(new AppStyle(qApp->style()));
        mAppStyleInstalled = true;
    }
    qApp->setStyleSheet(dialogButtonStyleSheet());
}

void GUIResources::loadIcons() {
    mIconMap.clear();
    const QDir dir(mIconDir);
    const QStringList files = dir.entryList(QStringList() << "*.svg", QDir::Files);
    for (const QString& name : files) {
        if (name.endsWith("-active.svg")) continue; // variant, not a stem
        const QString stem = name.left(name.size() - 4); // strip .svg
        QIcon icon;
        // Rest (Normal/Off) and active (Normal/On): a checked QPushButton
        // renders the On pixmap automatically (QCommonStyle maps State_On to
        // QIcon::On), so every toggle button gets the max-contrast glyph on
        // its accent fill without per-widget swap code. The -active file is
        // also registered as its own stem so iconActive() can hand it out.
        icon.addFile(dir.filePath(name), QSize(), QIcon::Normal, QIcon::Off);
        const QString activePath = dir.filePath(stem + "-active.svg");
        // Only toggle/hover-swap names carry an -active file (tintIcons
        // generates it for those); register the On state conditionally so a
        // plain icon never references a missing pixmap.
        if (QFile::exists(activePath)) {
            icon.addFile(activePath, QSize(), QIcon::Normal, QIcon::On);
            mIconMap.insert(stem + "-active", QIcon(activePath));
        }
        mIconMap.insert(stem, icon);
    }
}

void GUIResources::applyPalette(const theme::Colors& aColors) {
    palette.setColor(QPalette::Window, aColors.base);
    palette.setColor(QPalette::WindowText, aColors.text);
    palette.setColor(QPalette::Base, aColors.raised);
    palette.setColor(QPalette::AlternateBase, aColors.raisedHover);
    palette.setColor(QPalette::ToolTipBase, aColors.raised);
    palette.setColor(QPalette::ToolTipText, aColors.text);
    palette.setColor(QPalette::Text, aColors.text);
    palette.setColor(QPalette::Button, aColors.raised);
    palette.setColor(QPalette::ButtonText, aColors.text);
    palette.setColor(QPalette::BrightText, aColors.text);
    palette.setColor(QPalette::Link, aColors.accentBright);
    palette.setColor(QPalette::Highlight, aColors.textSelection);
    palette.setColor(QPalette::HighlightedText, aColors.selectionText);
}

QString GUIResources::dialogButtonStyleSheet() const {
    // Dialog buttons are styled app-wide (qApp->setStyleSheet) so parentless
    // dialogs (QMessageBox) inherit them; the template lives alongside the
    // other .ssa files and is token-substituted the same way.
    return mTheme.loadStylesheet("dialogbutton.ssa");
}

} // namespace gui
