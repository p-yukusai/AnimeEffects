#include "theme/Icons.h"
#include <QDir>
#include <QFile>
#include <QHash>

namespace theme {

namespace {
const QHash<QString, IconRole>& roleTable() {
    static const QHash<QString, IconRole> k{
        {QStringLiteral("animation"), IconRole::Text},
        {QStringLiteral("audio"), IconRole::Text},
        {QStringLiteral("bind"), IconRole::Text},
        {QStringLiteral("bone"), IconRole::Text},
        {QStringLiteral("branch_closed"), IconRole::Muted},
        {QStringLiteral("branch_closed_hover"), IconRole::Accent},
        {QStringLiteral("branch_end"), IconRole::Hairline},
        {QStringLiteral("branch_end_arrow"), IconRole::Hairline},
        {QStringLiteral("branch_more"), IconRole::Hairline},
        {QStringLiteral("branch_more_arrow"), IconRole::Hairline},
        {QStringLiteral("branch_open"), IconRole::Muted},
        {QStringLiteral("branch_open_hover"), IconRole::Accent},
        {QStringLiteral("calendar_next"), IconRole::Text},
        {QStringLiteral("calendar_previous"), IconRole::Text},
        {QStringLiteral("caret-down"), IconRole::Muted},
        {QStringLiteral("caret-down-regular"), IconRole::Muted},
        {QStringLiteral("caret-right"), IconRole::Muted},
        {QStringLiteral("caret-right-regular"), IconRole::Muted},
        {QStringLiteral("check"), IconRole::Text},
        {QStringLiteral("checkbox_checked"), IconRole::Accent},
        {QStringLiteral("checkbox_checked_disabled"), IconRole::Disabled},
        {QStringLiteral("checkbox_indeterminate"), IconRole::Accent},
        {QStringLiteral("checkbox_indeterminate_disabled"), IconRole::Disabled},
        {QStringLiteral("checkbox_unchecked"), IconRole::Outline},
        {QStringLiteral("checkbox_unchecked_disabled"), IconRole::Disabled},
        {QStringLiteral("clear_text"), IconRole::Text},
        {QStringLiteral("close"), IconRole::Muted},
        {QStringLiteral("close_hover"), IconRole::Muted},
        {QStringLiteral("close_pressed"), IconRole::Accent},
        {QStringLiteral("computer"), IconRole::Text},
        {QStringLiteral("cursor"), IconRole::Text},
        {QStringLiteral("desktop"), IconRole::Text},
        {QStringLiteral("dialog_cancel"), IconRole::Text},
        {QStringLiteral("dialog_close"), IconRole::Text},
        {QStringLiteral("dialog_discard"), IconRole::Text},
        {QStringLiteral("dialog_help"), IconRole::Text},
        {QStringLiteral("dialog_no"), IconRole::Text},
        {QStringLiteral("dialog_ok"), IconRole::Text},
        {QStringLiteral("dialog_open"), IconRole::Text},
        {QStringLiteral("dialog_reset"), IconRole::Text},
        {QStringLiteral("dialog_save"), IconRole::Text},
        {QStringLiteral("disc_drive"), IconRole::Text},
        {QStringLiteral("door-open"), IconRole::Text},
        {QStringLiteral("down_arrow"), IconRole::Muted},
        {QStringLiteral("down_arrow_disabled"), IconRole::Disabled},
        {QStringLiteral("down_arrow_hover"), IconRole::Accent},
        {QStringLiteral("ease"), IconRole::Text},
        {QStringLiteral("eraser"), IconRole::Text},
        {QStringLiteral("eye-close"), IconRole::Muted},
        {QStringLiteral("eye-open"), IconRole::Muted},
        {QStringLiteral("faders-horizontal"), IconRole::Text},
        {QStringLiteral("fast"), IconRole::Text},
        {QStringLiteral("ffd"), IconRole::Text},
        {QStringLiteral("file"), IconRole::Text},
        {QStringLiteral("file-dim"), IconRole::Muted},
        {QStringLiteral("file_dialog_contents"), IconRole::Text},
        {QStringLiteral("file_dialog_detailed"), IconRole::Text},
        {QStringLiteral("file_dialog_end"), IconRole::Text},
        {QStringLiteral("file_dialog_info"), IconRole::Text},
        {QStringLiteral("file_dialog_list"), IconRole::Text},
        {QStringLiteral("file_dialog_start"), IconRole::Text},
        {QStringLiteral("file_link"), IconRole::Text},
        {QStringLiteral("flip"), IconRole::Text},
        {QStringLiteral("floppy_drive"), IconRole::Text},
        {QStringLiteral("folder"), IconRole::Text},
        {QStringLiteral("folder_link"), IconRole::Text},
        {QStringLiteral("folder_open"), IconRole::Text},
        {QStringLiteral("hard_drive"), IconRole::Text},
        {QStringLiteral("hardness-1"), IconRole::Text},
        {QStringLiteral("hardness-2"), IconRole::Text},
        {QStringLiteral("hardness-3"), IconRole::Text},
        {QStringLiteral("help"), IconRole::Text},
        {QStringLiteral("hmovetoolbar"), IconRole::Muted},
        {QStringLiteral("home_directory"), IconRole::Text},
        {QStringLiteral("hseptoolbar"), IconRole::Muted},
        {QStringLiteral("image"), IconRole::Text},
        {QStringLiteral("influence"), IconRole::Text},
        {QStringLiteral("left_arrow"), IconRole::Muted},
        {QStringLiteral("left_arrow_disabled"), IconRole::Disabled},
        {QStringLiteral("left_arrow_hover"), IconRole::Accent},
        {QStringLiteral("loop"), IconRole::Text},
        {QStringLiteral("maximize"), IconRole::Muted},
        {QStringLiteral("menu"), IconRole::Muted},
        {QStringLiteral("mesh"), IconRole::Text},
        {QStringLiteral("message_critical"), IconRole::Text},
        {QStringLiteral("message_information"), IconRole::Text},
        {QStringLiteral("message_question"), IconRole::Text},
        {QStringLiteral("message_warning"), IconRole::Text},
        {QStringLiteral("minimize"), IconRole::Muted},
        {QStringLiteral("minus"), IconRole::Muted},
        {QStringLiteral("move"), IconRole::Text},
        {QStringLiteral("move-centroid"), IconRole::Text},
        {QStringLiteral("network_drive"), IconRole::Text},
        {QStringLiteral("paint-brush"), IconRole::Text},
        {QStringLiteral("pause"), IconRole::Text},
        {QStringLiteral("pencil"), IconRole::Text},
        {QStringLiteral("play"), IconRole::Text},
        {QStringLiteral("plus"), IconRole::Muted},
        {QStringLiteral("pose"), IconRole::Text},
        {QStringLiteral("radio_checked"), IconRole::Accent},
        {QStringLiteral("radio_checked_disabled"), IconRole::Disabled},
        {QStringLiteral("radio_unchecked"), IconRole::Outline},
        {QStringLiteral("radio_unchecked_disabled"), IconRole::Disabled},
        {QStringLiteral("reset-rotation"), IconRole::Text},
        {QStringLiteral("restore"), IconRole::Muted},
        {QStringLiteral("rewind"), IconRole::Text},
        {QStringLiteral("right_arrow"), IconRole::Muted},
        {QStringLiteral("right_arrow_disabled"), IconRole::Disabled},
        {QStringLiteral("right_arrow_hover"), IconRole::Accent},
        {QStringLiteral("rotate-ccw"), IconRole::Text},
        {QStringLiteral("rotate-cw"), IconRole::Text},
        {QStringLiteral("shade"), IconRole::Muted},
        {QStringLiteral("show-mesh"), IconRole::Text},
        {QStringLiteral("sizegrip"), IconRole::Muted},
        {QStringLiteral("srt"), IconRole::Text},
        {QStringLiteral("step"), IconRole::Text},
        {QStringLiteral("step-back"), IconRole::Text},
        {QStringLiteral("transparent"), IconRole::Muted},
        {QStringLiteral("trash"), IconRole::Text},
        {QStringLiteral("undock"), IconRole::Muted},
        {QStringLiteral("undock_hover"), IconRole::Muted},
        {QStringLiteral("undock_hover_pressed"), IconRole::Accent},
        {QStringLiteral("unshade"), IconRole::Muted},
        {QStringLiteral("up_arrow"), IconRole::Muted},
        {QStringLiteral("up_arrow_disabled"), IconRole::Disabled},
        {QStringLiteral("up_arrow_hover"), IconRole::Accent},
        {QStringLiteral("vline"), IconRole::Hairline},
        {QStringLiteral("vmovetoolbar"), IconRole::Muted},
        {QStringLiteral("vseptoolbar"), IconRole::Muted},
        {QStringLiteral("window_close"), IconRole::Muted},
    };
    return k;
}
} // namespace

QColor roleColor(IconRole aRole, const Colors& aColors) {
    switch (aRole) {
    case IconRole::Text: return aColors.icon;
    case IconRole::Muted: return aColors.textMuted;
    case IconRole::Hairline: return aColors.hairline;
    case IconRole::Accent: return aColors.focus;
    case IconRole::Outline: return aColors.outline;
    case IconRole::Disabled: return aColors.textDisabled;
    }
    return aColors.icon;
}

IconRole iconRole(const QString& aStem) {
    return roleTable().value(aStem, IconRole::Text);
}

static QString gIconDir;
QString iconDir() { return gIconDir; }
void setIconDir(const QString& aDir) { gIconDir = aDir; }

// Tint the canonical currentColor set into the scratch dir. Multi-color glyphs
// (message/file-dialog) keep their semantic accents; only currentColor is
// replaced, so they stay correct.
void tintIcons(const QString& aSrcDir, const QString& aDstDir, const Colors& aColors) {
    const QDir src(aSrcDir);
    const QStringList files = src.entryList(QStringList() << "*.svg", QDir::Files);
    // Wipe the scratch dir first so glyphs that left the canonical set (or
    // lost their -active pair) do not linger across app versions and get
    // re-registered by loadIcons.
    QDir(aDstDir).removeRecursively();
    QDir().mkpath(aDstDir);
    for (const QString& name : files) {
        QFile f(src.filePath(name));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString svg = QString::fromUtf8(f.readAll());
        f.close();
        const QString stem = name.left(name.size() - 4);
        // the canonical tint (role color)
        QString out = svg;
        out.replace("currentColor", roleColor(iconRole(stem), aColors).name());
        QFile of(aDstDir + "/" + name);
        if (of.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            of.write(out.toUtf8());
        }
        // the active-state tint (checked buttons: max-contrast content color).
        // The QIcon pairs base (rest) with this (active) so a checked button
        // renders the active glyph automatically via the QIcon On state.
        QString active = svg;
        active.replace("currentColor", aColors.accentText.name());
        QFile af(aDstDir + "/" + stem + "-active.svg");
        if (af.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            af.write(active.toUtf8());
        }
    }
}

} // namespace theme
