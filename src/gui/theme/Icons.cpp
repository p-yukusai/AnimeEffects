#include "theme/Icons.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

namespace theme {

namespace {
const QHash<QString, IconRole>& roleTable() {
    static const QHash<QString, IconRole> k{
        {QStringLiteral("images"), IconRole::Text},
        {QStringLiteral("speaker-high"), IconRole::Text},
        {QStringLiteral("link"), IconRole::Text},
        {QStringLiteral("bone"), IconRole::Text},
        {QStringLiteral("caret-down"), IconRole::Muted},
        {QStringLiteral("caret-down-bold"), IconRole::Muted},
        {QStringLiteral("caret-down-bold-disabled"), IconRole::Disabled},
        {QStringLiteral("caret-down-bold-hover"), IconRole::Accent},
        {QStringLiteral("caret-right"), IconRole::Muted},
        {QStringLiteral("caret-right-bold"), IconRole::Muted},
        {QStringLiteral("caret-up-bold"), IconRole::Muted},
        {QStringLiteral("caret-up-bold-disabled"), IconRole::Disabled},
        {QStringLiteral("caret-up-bold-hover"), IconRole::Accent},
        {QStringLiteral("check-bold"), IconRole::Text},
        {QStringLiteral("checkbox_checked"), IconRole::Accent},
        {QStringLiteral("checkbox_checked_disabled"), IconRole::Disabled},
        {QStringLiteral("checkbox_indeterminate"), IconRole::Accent},
        {QStringLiteral("checkbox_indeterminate_disabled"), IconRole::Disabled},
        {QStringLiteral("checkbox_unchecked"), IconRole::Outline},
        {QStringLiteral("checkbox_unchecked_disabled"), IconRole::Disabled},
        {QStringLiteral("close"), IconRole::Muted},
        {QStringLiteral("close_hover"), IconRole::Muted},
        {QStringLiteral("close_pressed"), IconRole::Accent},
        {QStringLiteral("x"), IconRole::Muted}, // phosphor x (popover close)
        {QStringLiteral("x_hover"), IconRole::Text},
        // Layout toggles (the header's right corner): one quiet group, dimmer
        // than the viewport settings beside it. A slot that is showing uses the
        // FILLED phosphor sidebar and an empty one the outline; the state is the
        // glyph's form, so both variants take the same dim tint and the checked
        // state is not a second, brighter signal.
        {QStringLiteral("sidebar-left"), IconRole::MutedDim},
        {QStringLiteral("sidebar-right"), IconRole::MutedDim},
        {QStringLiteral("sidebar-top"), IconRole::MutedDim},
        {QStringLiteral("sidebar-bottom"), IconRole::MutedDim},
        {QStringLiteral("sidebar-left-fill"), IconRole::MutedDim},
        {QStringLiteral("sidebar-right-fill"), IconRole::MutedDim},
        {QStringLiteral("sidebar-top-fill"), IconRole::MutedDim},
        {QStringLiteral("sidebar-bottom-fill"), IconRole::MutedDim},
        {QStringLiteral("hand"), IconRole::Text},
        {QStringLiteral("dialog_cancel"), IconRole::Text},
        {QStringLiteral("dialog_close"), IconRole::Text},
        {QStringLiteral("dialog_discard"), IconRole::Text},
        {QStringLiteral("dialog_help"), IconRole::Text},
        {QStringLiteral("dialog_no"), IconRole::Text},
        {QStringLiteral("dialog_ok"), IconRole::Text},
        {QStringLiteral("dialog_open"), IconRole::Text},
        {QStringLiteral("dialog_reset"), IconRole::Text},
        {QStringLiteral("dialog_save"), IconRole::Text},
        {QStringLiteral("arrows-horizontal"), IconRole::MutedDim},
        {QStringLiteral("arrows-vertical"), IconRole::MutedDim},
        {QStringLiteral("stack"), IconRole::MutedDim},
        {QStringLiteral("circle-half"), IconRole::MutedDim},
        {QStringLiteral("grid-four"), IconRole::MutedDim},
        {QStringLiteral("angle"), IconRole::MutedDim},
        {QStringLiteral("arrows-out-line-horizontal"), IconRole::Text},
        {QStringLiteral("down_arrow"), IconRole::Muted},
        {QStringLiteral("down_arrow_disabled"), IconRole::Disabled},
        {QStringLiteral("down_arrow_hover"), IconRole::Accent},
        {QStringLiteral("ease"), IconRole::Text},
        {QStringLiteral("envelope"), IconRole::Text},
        {QStringLiteral("eraser"), IconRole::Text},
        {QStringLiteral("eye-close"), IconRole::Muted},
        {QStringLiteral("eye-open"), IconRole::Muted},
        {QStringLiteral("faders-horizontal"), IconRole::Text},
        {QStringLiteral("ffd"), IconRole::Text},
        {QStringLiteral("file"), IconRole::Text},
        {QStringLiteral("file-dashed"), IconRole::Muted},
        {QStringLiteral("flip-horizontal"), IconRole::Text},
        {QStringLiteral("folder"), IconRole::Text},
        {QStringLiteral("hardness-1"), IconRole::Text},
        {QStringLiteral("hardness-2"), IconRole::Text},
        {QStringLiteral("hardness-3"), IconRole::Text},
        {QStringLiteral("image"), IconRole::Text},
        {QStringLiteral("broadcast"), IconRole::Text},
        {QStringLiteral("left_arrow"), IconRole::Muted},
        {QStringLiteral("left_arrow_disabled"), IconRole::Disabled},
        {QStringLiteral("left_arrow_hover"), IconRole::Accent},
        {QStringLiteral("arrows-clockwise"), IconRole::Text},
        {QStringLiteral("triangle"), IconRole::Text},
        {QStringLiteral("message_critical"), IconRole::Text},
        {QStringLiteral("message_information"), IconRole::Text},
        {QStringLiteral("message_question"), IconRole::Text},
        {QStringLiteral("message_warning"), IconRole::Text},
        {QStringLiteral("minus"), IconRole::Muted},
        {QStringLiteral("minus-bold"), IconRole::Muted},
        {QStringLiteral("navigation-arrow"), IconRole::Text},
        {QStringLiteral("crosshair"), IconRole::Text},
        {QStringLiteral("paint-brush"), IconRole::Text},
        {QStringLiteral("paint-envelope"), IconRole::Text},
        {QStringLiteral("pause"), IconRole::Text},
        {QStringLiteral("pencil-simple"), IconRole::Text},
        {QStringLiteral("play"), IconRole::Text},
        {QStringLiteral("plus"), IconRole::Muted},
        {QStringLiteral("plus-bold"), IconRole::Muted},
        {QStringLiteral("pose"), IconRole::Text},
        {QStringLiteral("radio_checked"), IconRole::Accent},
        {QStringLiteral("radio_checked_disabled"), IconRole::Disabled},
        {QStringLiteral("radio_unchecked"), IconRole::Outline},
        {QStringLiteral("radio_unchecked_disabled"), IconRole::Disabled},
        {QStringLiteral("dot-outline"), IconRole::Text},
        {QStringLiteral("right_arrow"), IconRole::Muted},
        {QStringLiteral("right_arrow_disabled"), IconRole::Disabled},
        {QStringLiteral("right_arrow_hover"), IconRole::Accent},
        {QStringLiteral("arrow-counter-clockwise"), IconRole::Text},
        {QStringLiteral("arrow-clockwise"), IconRole::Text},
        {QStringLiteral("cube"), IconRole::Text},
        // The header's view settings are window controls, not dock content: one
        // step below the dock's Text glyphs and one above the slot toggles'
        // MutedDim. The -quiet names resolve to the same source files.
        {QStringLiteral("cube-quiet"), IconRole::Muted},
        {QStringLiteral("image-quiet"), IconRole::Muted},
        {QStringLiteral("arrow-counter-clockwise-quiet"), IconRole::Muted},
        {QStringLiteral("arrow-clockwise-quiet"), IconRole::Muted},
        {QStringLiteral("dot-outline-quiet"), IconRole::Muted},
        {QStringLiteral("flip-horizontal-quiet"), IconRole::Muted},
        {QStringLiteral("arrows-out-cardinal"), IconRole::Text},
        {QStringLiteral("skip-forward"), IconRole::Text},
        {QStringLiteral("skip-back"), IconRole::Text},
        {QStringLiteral("transparent"), IconRole::Muted},
        {QStringLiteral("undock"), IconRole::Muted},
        {QStringLiteral("undock_hover"), IconRole::Muted},
        {QStringLiteral("undock_hover_pressed"), IconRole::Accent},
        {QStringLiteral("up_arrow"), IconRole::Muted},
        {QStringLiteral("up_arrow_disabled"), IconRole::Disabled},
        {QStringLiteral("up_arrow_hover"), IconRole::Accent},
    };
    return k;
}
} // namespace

QColor roleColor(IconRole aRole, const Colors& aColors) {
    switch (aRole) {
    case IconRole::Text: return aColors.icon;
    case IconRole::Muted: return aColors.textMuted;
    case IconRole::MutedDim: return aColors.textMutedDim;
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

// Icons that render on a checkable button or swap to the active glyph on
// hover — the transport row's hover icons use iconActive(), and checked
// mode/view/tool buttons render the QIcon On state. Only these names get an
// -active tint; the other glyphs never show a checked state, so generating
// (and registering) -active copies for them was pure scratch-dir weight.
const QSet<QString>& activeVariantNames() {
    static const QSet<QString> s = {
        // transport row (hover swap via iconActive): all eight buttons
        "skip-back", "step-back", "play", "step-forward", "skip-forward",
        "arrows-clockwise", "faders-horizontal", "speaker-high",
        // mode panel (checkable)
        "hand", "arrows-out-cardinal", "bone", "pose", "triangle", "ffd",
        // view panel (checkable)
        "cube", "image", "arrow-clockwise", "arrow-counter-clockwise",
        "dot-outline", "flip-horizontal",
        // tool mode groups (checkable SingleOutItem buttons)
        "plus-bold", "minus-bold", "navigation-arrow", "link", "broadcast",
        "paint-brush", "paint-envelope", "envelope", "eraser", "pencil-simple", "hardness-1", "hardness-2",
        "hardness-3", "crosshair",
        // (the header's sidebar toggles are deliberately absent: their checked
        // state is the filled glyph, not an -active tint)
    };
    return s;
}

bool needsActiveVariant(const QString& aName) {
    return activeVariantNames().contains(aName);
}

static QString gIconDir;
QString iconDir() { return gIconDir; }
void setIconDir(const QString& aDir) { gIconDir = aDir; }

// Tint the canonical currentColor set (root icons plus the ph/ phosphor
// imports) into the scratch dir. Multi-color glyphs (message/file-dialog)
// keep their semantic accents; only currentColor is replaced, so they stay
// correct.
//
// The name set is the source files plus every roleTable entry. State
// variants (-hover/-disabled/-pressed/-quiet, root underscore and ph/ hyphen
// conventions) share the base glyph's source file — the role table tints
// each name with its own state color, so the byte-identical duplicate
// sources are not shipped. Each name still gets its own output (the QSS
// references @icondir@/<name>.svg); the -active pair is generated only for
// names that render checked/hover states (activeVariantNames).
void tintIcons(const QString& aSrcDir, const QString& aDstDir, const Colors& aColors) {
    const auto exists = [&](const QString& aStem) {
        return QFile::exists(aSrcDir + "/" + aStem + ".svg")
            || QFile::exists(aSrcDir + "/ph/" + aStem + ".svg");
    };
    // The base glyph for a state name: the name's own file, else the name
    // with one state suffix stripped (checked against the files on disk, so
    // a name that merely ends like a suffix still resolves to itself).
    const auto sourceStem = [&](const QString& aName) {
        if (exists(aName)) return aName;
        // -quiet is not a source file: the header's view-setting variants share
        // the dock glyph and differ only in the tint role above.
        const QStringList suffixes = {"-hover", "-disabled", "-pressed", "-quiet",
                                      "_hover", "_disabled", "_pressed"};
        for (const QString& s : suffixes) {
            if (aName.endsWith(s)) {
                const QString base = aName.left(aName.size() - s.size());
                if (exists(base)) return base;
            }
        }
        return aName;
    };

    // Canonical names: every source file (root + ph/) plus every roleTable
    // name (state variants live only there once their source files dedupe).
    QStringList names;
    const auto addDir = [&](const QString& aDir) {
        for (const QString& n : QDir(aDir).entryList(QStringList() << "*.svg", QDir::Files))
            names << QFileInfo(n).completeBaseName();
    };
    addDir(aSrcDir);
    addDir(aSrcDir + "/ph");
    for (auto it = roleTable().keyBegin(); it != roleTable().keyEnd(); ++it)
        names << *it;
    names.removeDuplicates();
    names.sort();

    // Wipe the scratch dir first so glyphs that left the canonical set (or
    // lost their -active pair) do not linger across app versions and get
    // re-registered by loadIcons.
    QDir(aDstDir).removeRecursively();
    QDir().mkpath(aDstDir);
    for (const QString& name : names) {
        const QString stem = sourceStem(name);
        QFile f(aSrcDir + "/" + stem + ".svg");
        if (!f.exists()) f.setFileName(aSrcDir + "/ph/" + stem + ".svg");
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString svg = QString::fromUtf8(f.readAll());
        f.close();
        // the canonical tint (role color)
        QString out = svg;
        out.replace("currentColor", roleColor(iconRole(name), aColors).name());
        QFile of(aDstDir + "/" + name + ".svg");
        if (of.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            of.write(out.toUtf8());
        }
        // the active-state tint (checked buttons: max-contrast content color).
        // The QIcon pairs base (rest) with this (active) so a checked button
        // renders the active glyph automatically via the QIcon On state. Only
        // names on checkable buttons / hover swaps get one (activeVariantNames).
        if (needsActiveVariant(name)) {
            QString active = svg;
            active.replace("currentColor", aColors.accentText.name());
            QFile af(aDstDir + "/" + name + "-active.svg");
            if (af.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                af.write(active.toUtf8());
            }
        }
    }
}

} // namespace theme
