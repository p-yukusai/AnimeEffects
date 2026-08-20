#ifndef GUI_THEME_COLORS_H
#define GUI_THEME_COLORS_H

#include <QColor>
#include <QString>

#include "TailwindPalette.h" // generated: the full Tailwind v4 table (pure data)

namespace theme {

// ---------------------------------------------------------------------------
// Design tokens are cells of the Tailwind v4 table (see TailwindPalette.h):
// the accent setting picks the row (base color), the design tokens are
// columns (lightness steps), and everything non-accent reads the neutral row
// (achromatic). Cells are sRGB hex, parsed straight into QColor — no
// conversion or gamut logic.
//
// This file holds the app logic the generated header deliberately excludes:
// the token roles, the authored hue->row and token->column mappings, and the
// token construction.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Selectable accent colors: a curated subset of the Tailwind v4 rows (the
// full table lives in TailwindPalette.h). AccentColor values are contiguous
// 0..kAccentCount-1; kAccentRow maps each to its row index in kTailwindRows.
// Settings persist the row name (accentName).
// ---------------------------------------------------------------------------
enum AccentColor {
    kAccentRed, kAccentOrange, kAccentYellow, kAccentGreen,
    kAccentTeal, kAccentCyan, kAccentViolet, kAccentFuchsia,
    kAccentCount
};
inline constexpr AccentColor kDefaultAccent = kAccentViolet;

// AccentColor -> Tailwind row index (kTailwindRows order: red, orange, amber,
// yellow, lime, green, emerald, teal, cyan, sky, blue, violet, ...).
static constexpr int kAccentRow[] = {0, 1, 3, 5, 7, 8, 12, 14};
static_assert(sizeof(kAccentRow) / sizeof(kAccentRow[0]) == kAccentCount,
              "kAccentRow must have one entry per AccentColor");

inline const char* accentName(AccentColor aAccent) {
    static constexpr const char* kNames[] = {
        "red", "orange", "yellow", "green", "teal", "cyan", "violet", "fuchsia" };
    static_assert(sizeof(kNames) / sizeof(kNames[0]) == kAccentCount,
                  "kNames must have one entry per AccentColor");
    return kNames[aAccent];
}

// Row name (settings value) -> AccentColor; violet for unknown names.
inline AccentColor accentFromName(const QString& aName) {
    for (int i = 0; i < kAccentCount; ++i) {
        if (aName == QLatin1String(accentName((AccentColor)i))) return (AccentColor)i;
    }
    return kDefaultAccent;
}

// Row order in the generated table: red, orange, amber, yellow, lime, green,
// emerald, teal, cyan, sky, blue, indigo, violet, purple, fuchsia, pink,
// rose, slate, gray, zinc, neutral, stone, mauve, olive, mist, taupe.
inline constexpr int kTailwindNeutralRow = 20; // the achromatic neutral row

// Token roles; the column maps are indexed by these.
enum TokenRole {
    kBase, kRecessed, kRaised, kRaisedHover, kActive, kActiveHover,
    kHairline, kHairlineHover, kOutline, kHandleBorder, kFocus, kHover,
    kText, kTextMuted, kTextDisabled, kIcon,
    kSelection, kSelectionText, kTextSelection, kAccent, kAccentSwatch, kAccentBright, kAccentHover, kAccentText,
    kFloaterBody, kFloaterEdge,
    kGuideLine, // dashed guides drawn on the recessed canvas (spline editor)
    kTokenCount
};

// Token columns (lightness steps into kTailwindRows), one line per role in
// enum order. Light reads the pale columns, dark the deep ones; higher
// column = darker (inverse lightness). Edit by finding the role's line.
// The size is deduced so the asserts below catch a role added to the enum
// without a column entry (or a stray extra entry).
static constexpr int kTailwindLight[] = {
    /* kBase              */ 0,
    /* kRecessed          */ 1,
    /* kRaised            */ 2,
    /* kRaisedHover       */ 3,
    /* kActive            */ 2,
    /* kActiveHover       */ 2,
    /* kHairline          */ 2,
    /* kHairlineHover     */ 3,
    /* kOutline           */ 3,
    /* kHandleBorder      */ 2,
    /* kFocus             */ 6,
    /* kHover             */ 4,
    /* kText              */ 9,
    /* kTextMuted         */ 5,
    /* kTextDisabled      */ 4,
    /* kIcon              */ 9,
    /* kSelection         */ 3,
    /* kSelectionText     */ 9,
    /* kTextSelection     */ 3,
    /* kAccent            */ 2,
    /* kAccentSwatch      */ 5,
    /* kAccentBright      */ 6,
    /* kAccentHover       */ 3,
    /* kAccentText        */ 9,
    /* kFloaterBody       */ 0,
    /* kFloaterEdge       */ 2, // popup border; tracks hairline
    /* kGuideLine         */ 8, // a mid-dark line that reads on the pale
                                 // recessed canvas (light theme; the 1px
                                 // dashed AA halves the nominal contrast)
};
static_assert(sizeof(kTailwindLight) / sizeof(kTailwindLight[0]) == kTokenCount,
              "kTailwindLight must have one column per TokenRole");
static constexpr int kTailwindDark[] = {
    /* kBase              */ 8,
    /* kRecessed          */ 9,
    /* kRaised            */ 7,
    /* kRaisedHover       */ 6,
    /* kActive            */ 6,
    /* kActiveHover       */ 6,
    /* kHairline          */ 7,
    /* kHairlineHover     */ 6,
    /* kOutline           */ 6,
    /* kHandleBorder      */ 5,
    /* kFocus             */ 4,
    /* kHover             */ 5,
    /* kText              */ 1,
    /* kTextMuted         */ 4,
    /* kTextDisabled      */ 5,
    /* kIcon              */ 2,
    /* kSelection         */ 7,
    /* kSelectionText     */ 1,
    /* kTextSelection     */ 5,
    /* kAccent            */ 7,
    /* kAccentSwatch      */ 5,
    /* kAccentBright      */ 4,
    /* kAccentHover       */ 6,
    /* kAccentText        */ 1,
    /* kFloaterBody       */ 9,
    /* kFloaterEdge       */ 8, // normal edge; the body is the sunken surface
    /* kGuideLine         */ 4, // a mid-light line that reads on the dark
                                 // recessed canvas
};
static_assert(sizeof(kTailwindDark) / sizeof(kTailwindDark[0]) == kTokenCount,
              "kTailwindDark must have one column per TokenRole");

// Hex cell -> QColor, optional alpha (used by the translucent selection).
inline QColor paletteColor(const char* aHex, double aAlpha = 1.0) {
    QColor c(QString::fromLatin1(aHex));
    if (aAlpha < 1.0) c.setAlphaF((float)aAlpha);
    return c;
}

// ---------------------------------------------------------------------------
// Per-key timeline colors: one Tailwind row per key type at a fixed column,
// so keys read by hue and stay distinguishable at any zoom. The hues follow
// the classic timeline rainbow (move=indigo ... blur=fuchsia); baseKey is
// the neutral key color (the renderer's default brush) and flips polarity
// per theme — its column is overridden in light()/dark() below.
// ---------------------------------------------------------------------------
enum KeyColorToken { kKeyBase, kKeyMove, kKeyRotate, kKeyScale, kKeyDepth,
                     kKeyOpa, kKeyBone, kKeyPose, kKeyMesh, kKeyFFD,
                     kKeyImage, kKeyHSV, kKeyBlur, kKeyCount };
static constexpr int kKeyColorRow[] = {
    kTailwindNeutralRow, // baseKey  (neutral)
    11,                  // moveKey  (indigo)
    13,                  // rotateKey (purple)
    14,                  // scaleKey (fuchsia)
    16,                  // depthKey (rose)
    2,                   // opaKey   (amber)
    4,                   // boneKey  (lime)
    5,                   // poseKey  (green)
    6,                   // meshKey  (emerald)
    8,                   // FFDKey   (cyan)
    9,                   // imageKey (sky)
    11,                  // HSVKey   (indigo)
    14,                  // blurKey  (fuchsia)
};
static_assert(sizeof(kKeyColorRow) / sizeof(kKeyColorRow[0]) == kKeyCount,
              "kKeyColorRow must have one row per KeyColorToken");
// Every key type shares one lightness column per theme, so the keys read at
// the same brightness and stay distinguishable by hue alone (a mix of deep
// and pale steps read as inconsistent). The mid-vivid steps mirror
// kAccentBright (light 6, dark 4): a step darker on pale lanes, a step
// lighter on dark lanes.
static constexpr int kKeyColorColumnLight = 6;
static constexpr int kKeyColorColumnDark = 4;

// ---------------------------------------------------------------------------
// Design tokens for an appearance (light/dark x accent hue).
//
// This is the single source of truth for every surface the custom-painted
// widgets draw, for the QPalette, for the stylesheet templates, and for the
// runtime-tinted icons. A token is a palette cell: the selected base-color
// row for accents, the neutral row (achromatic) for everything else, at the
// column (lightness step) that fits its role and theme.
//
// Border highlight splits by state: hover is neutral (`hover`), active/focus
// is the accent (`focus` == accentBright).
// ---------------------------------------------------------------------------
struct Colors {
    QColor base;          // window, docks, panels, chrome, menu floor
    QColor recessed;      // sunken: viewport, disabled fills, pressed, scrollbar track, log
    QColor raised;        // elevated: input fields, buttons, cards
    QColor raisedHover;   // hover state on raised surfaces
    QColor active;        // neutral checked / pressed fill
    QColor activeHover;   // hover on active
    QColor hairline;      // separators, splitter/dock lines, ruler line, row seams
    QColor hairlineHover; // one step up from hairline (hover, lane separators)
    QColor outline;       // input/button borders, scrollbar pill at rest
    QColor handleBorder;  // slider knob rest border: a border tone, not the
                          // strong checked-fill token (own role so it can
                          // diverge from the border family later)
    QColor focus;         // focus ring / active border (accent hue)
    QColor hover;         // hover border, splitter drag, scrollbar pill active (neutral)
    QColor text;          // primary text
    QColor textMuted;     // secondary text
    QColor textDisabled;  // disabled text
    QColor icon;          // resting content on buttons (icons, pill text):
                          // dimmer than `text` so the active state reads
    QColor selection;     // brand selection / highlight fill
    QColor selectionText; // text on selection
    QColor textSelection; // text-input selection highlight (over the field,
                          // which is lighter than the tree floor in dark)
    QColor accent;        // brand checked/active fill (active-mode, link)
    QColor accentSwatch;  // settings accent preview: the hue at a mid
                          // lightness so the picker reads on light and dark
    QColor accentBright;  // brand bright edge (playhead, marquee, focus ring)
    QColor accentHover;   // menu/combo popup hover fill: a stronger accent
                          // tint than `selection` (which stays quiet)
    QColor accentText;    // content on accent fills (checked icons/pill text):
                          // max-contrast (white in dark, black in light)
    QColor floaterBody;   // menu/combo popup panel: dark stays sunken, light
                          // flips to white (the proxy rounds the corners)
    QColor floaterEdge;   // popup panel edge: light tracks the hairline token
                          // (borders match separators), dark keeps its own
                          // edge — the body is the sunken surface
    QColor guideLine;     // dashed guides on the recessed canvas (spline
                          // editor): mid-toned per theme so they read on the
                          // sunken surface, where the hairline family fades
    // Per-key colors for easier identification: one Tailwind row per key
    // type (kKeyColorRow) at a shared per-theme column, so all keys read at
    // the same lightness; baseKey is the neutral key color.
    QColor baseKey; // Equal to TimeKeyType_TERM
    QColor moveKey;
    QColor rotateKey;
    QColor scaleKey; // Scale, rotate & transform
    QColor depthKey;
    QColor opaKey;
    QColor boneKey;
    QColor poseKey;
    QColor meshKey;
    QColor FFDKey;
    QColor imageKey;
    QColor HSVKey;
    QColor blurKey;

    bool isDark; // which elevation model the tokens were computed with

    static Colors light(AccentColor aAccent = kDefaultAccent) {
        const auto& a = kTailwindRows[kAccentRow[aAccent]]; // accent row
        const auto& n = kTailwindRows[kTailwindNeutralRow]; // surfaces + content
        Colors c{
            paletteColor(n[kTailwindLight[kBase]]),        // base
            paletteColor(n[kTailwindLight[kRecessed]]),    // recessed
            paletteColor(n[kTailwindLight[kRaised]]),      // raised
            paletteColor(n[kTailwindLight[kRaisedHover]]), // raisedHover
            paletteColor(n[kTailwindLight[kActive]]),      // active
            paletteColor(n[kTailwindLight[kActiveHover]]), // activeHover
            paletteColor(n[kTailwindLight[kHairline]]),    // hairline
            paletteColor(n[kTailwindLight[kHairlineHover]]), // hairlineHover
            paletteColor(n[kTailwindLight[kOutline]]),     // outline
            paletteColor(n[kTailwindLight[kHandleBorder]]), // handleBorder
            paletteColor(a[kTailwindLight[kFocus]]),       // focus (accent edge)
            paletteColor(n[kTailwindLight[kHover]]),       // hover
            paletteColor(n[kTailwindLight[kText]]),        // text
            paletteColor(n[kTailwindLight[kTextMuted]]),   // textMuted
            paletteColor(n[kTailwindLight[kTextDisabled]]), // textDisabled
            paletteColor(n[kTailwindLight[kIcon]]),        // icon
            // selection: the accent fill at a fixed 40% opacity — a
            // translucent overlay, not a darker color. Every consumer
            // (QSS, palette, timeline) draws this same token; theme
            // differences live in the column indices, not per-widget alpha.
            paletteColor(a[kTailwindLight[kSelection]], 0.40), // selection
            paletteColor(n[kTailwindLight[kSelectionText]]), // selectionText
            paletteColor(a[kTailwindLight[kTextSelection]], 0.40), // textSelection
            paletteColor(a[kTailwindLight[kAccent]]),     // accent (pale fill)
            paletteColor(a[kTailwindLight[kAccentSwatch]]), // accentSwatch
            paletteColor(a[kTailwindLight[kAccentBright]]), // accentBright (edge)
            paletteColor(a[kTailwindLight[kAccentHover]]), // accentHover
            paletteColor(n[kTailwindLight[kAccentText]]), // accentText
            paletteColor(n[kTailwindLight[kFloaterBody]]), // floaterBody
            paletteColor(n[kTailwindLight[kFloaterEdge]]), // floaterEdge
            paletteColor(n[kTailwindLight[kGuideLine]]),   // guideLine
            // Per-key colors: kKeyColorRow at one shared column per theme
            // (kKeyColorColumnLight), so all keys read at the same lightness.
            paletteColor(kTailwindRows[kKeyColorRow[kKeyBase]][kKeyColorColumnLight]), // baseKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyMove]][kKeyColorColumnLight]), // moveKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyRotate]][kKeyColorColumnLight]), // rotateKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyScale]][kKeyColorColumnLight]), // scaleKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyDepth]][kKeyColorColumnLight]), // depthKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyOpa]][kKeyColorColumnLight]), // opaKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyBone]][kKeyColorColumnLight]), // boneKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyPose]][kKeyColorColumnLight]), // poseKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyMesh]][kKeyColorColumnLight]), // meshKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyFFD]][kKeyColorColumnLight]), // FFDKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyImage]][kKeyColorColumnLight]), // imageKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyHSV]][kKeyColorColumnLight]), // HSVKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyBlur]][kKeyColorColumnLight]), // blurKey
        };
        c.isDark = false;
        return c;
    }

    static Colors dark(AccentColor aAccent = kDefaultAccent) {
        const auto& a = kTailwindRows[kAccentRow[aAccent]];
        const auto& n = kTailwindRows[kTailwindNeutralRow];
        Colors c{
            paletteColor(n[kTailwindDark[kBase]]),        // base
            paletteColor(n[kTailwindDark[kRecessed]]),    // recessed
            paletteColor(n[kTailwindDark[kRaised]]),      // raised
            paletteColor(n[kTailwindDark[kRaisedHover]]), // raisedHover
            paletteColor(n[kTailwindDark[kActive]]),      // active
            paletteColor(n[kTailwindDark[kActiveHover]]), // activeHover
            paletteColor(n[kTailwindDark[kHairline]]),    // hairline
            paletteColor(n[kTailwindDark[kHairlineHover]]), // hairlineHover
            paletteColor(n[kTailwindDark[kOutline]]),     // outline
            paletteColor(n[kTailwindDark[kHandleBorder]]), // handleBorder
            paletteColor(a[kTailwindDark[kFocus]]),       // focus (accent edge)
            paletteColor(n[kTailwindDark[kHover]]),       // hover
            paletteColor(n[kTailwindDark[kText]]),        // text
            paletteColor(n[kTailwindDark[kTextMuted]]),   // textMuted
            paletteColor(n[kTailwindDark[kTextDisabled]]), // textDisabled
            paletteColor(n[kTailwindDark[kIcon]]),        // icon
            paletteColor(a[kTailwindDark[kSelection]], 0.40), // selection
            paletteColor(n[kTailwindDark[kSelectionText]]), // selectionText
            paletteColor(a[kTailwindDark[kTextSelection]], 0.40), // textSelection
            paletteColor(a[kTailwindDark[kAccent]]),      // accent (solid fill)
            paletteColor(a[kTailwindDark[kAccentSwatch]]), // accentSwatch
            paletteColor(a[kTailwindDark[kAccentBright]]), // accentBright (edge)
            paletteColor(a[kTailwindDark[kAccentHover]]), // accentHover
            paletteColor(n[kTailwindDark[kAccentText]]),  // accentText
            paletteColor(n[kTailwindDark[kFloaterBody]]), // floaterBody
            paletteColor(n[kTailwindDark[kFloaterEdge]]), // floaterEdge
            paletteColor(n[kTailwindDark[kGuideLine]]),   // guideLine
            // Per-key colors: kKeyColorRow at one shared column per theme
            // (kKeyColorColumnDark), so all keys read at the same lightness.
            paletteColor(kTailwindRows[kKeyColorRow[kKeyBase]][kKeyColorColumnDark]), // baseKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyMove]][kKeyColorColumnDark]), // moveKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyRotate]][kKeyColorColumnDark]), // rotateKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyScale]][kKeyColorColumnDark]), // scaleKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyDepth]][kKeyColorColumnDark]), // depthKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyOpa]][kKeyColorColumnDark]), // opaKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyBone]][kKeyColorColumnDark]), // boneKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyPose]][kKeyColorColumnDark]), // poseKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyMesh]][kKeyColorColumnDark]), // meshKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyFFD]][kKeyColorColumnDark]), // FFDKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyImage]][kKeyColorColumnDark]), // imageKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyHSV]][kKeyColorColumnDark]), // HSVKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyBlur]][kKeyColorColumnDark]), // blurKey
        };
        c.isDark = true;
        return c;
    }

    // The active palette. Computed once per appearance change by activate();
    // callers (custom-paint widgets, QSS substitution) read this instead of
    // re-deriving colors per paint event.
    static const Colors& current();

    // Recompute and cache the active palette for a (dark, hue) set.
    static void activate(bool aDark, AccentColor aAccent);

    // Fills @token@ placeholders (and @icondir@ for the runtime-tinted icon
    // directory) in a stylesheet template. The token values live only here.
    QString substitute(QString aTemplate, const QString& aIconDir) const {
        aTemplate.replace("@icondir@", aIconDir);
        aTemplate.replace("@base@", base.name());
        aTemplate.replace("@recessed@", recessed.name());
        aTemplate.replace("@raised@", raised.name());
        aTemplate.replace("@raisedHover@", raisedHover.name());
        aTemplate.replace("@active@", active.name());
        aTemplate.replace("@activeHover@", activeHover.name());
        aTemplate.replace("@hairline@", hairline.name());
        aTemplate.replace("@hairlineHover@", hairlineHover.name());
        aTemplate.replace("@outline@", outline.name());
        aTemplate.replace("@handleBorder@", handleBorder.name());
        aTemplate.replace("@focus@", focus.name());
        aTemplate.replace("@hover@", hover.name());
        aTemplate.replace("@text@", text.name());
        aTemplate.replace("@textMuted@", textMuted.name());
        aTemplate.replace("@textDisabled@", textDisabled.name());
        aTemplate.replace("@icon@", icon.name());
        // The QSS selection is the translucent token itself (HexArgb keeps
        // the 40% alpha in the substituted color — plain name() would drop
        // it and paint the opaque accent). The selected item's decoration
        // area (indentation/branch cell) is painted with this background a
        // second time, but the object tree disables that with
        // show-decoration-selected:0, so the alpha never compounds (the
        // palette path composites once).
        aTemplate.replace("@selection@", selection.name(QColor::HexArgb));
        // Text-input selection is its own token: it composites over the
        // raised field (lighter than the tree floor in dark), so it needs
        // a stronger column than the quiet item selection.
        aTemplate.replace("@textSelection@", textSelection.name(QColor::HexArgb));
        aTemplate.replace("@accent@", accent.name());
        aTemplate.replace("@accentHover@", accentHover.name());
        aTemplate.replace("@accentText@", accentText.name());
        // translucent accent overlay (hover tints): accentBright at 10% alpha
        // (accentBright stays saturated in both themes; the plain accent is
        // too pale at low alpha on light surfaces)
        aTemplate.replace("@accentOverlay@", "#1A" + accentBright.name().mid(1));
        aTemplate.replace("@accentBright@", accentBright.name());
        aTemplate.replace("@floaterBody@", floaterBody.name());
        aTemplate.replace("@floaterEdge@", floaterEdge.name());
        // translucent hover overlays: the text color at a fixed alpha
        aTemplate.replace("@overlay24@", "#24" + text.name().mid(1));
        aTemplate.replace("@overlay40@", "#40" + text.name().mid(1));
        return aTemplate;
    }
};

inline Colors& activeColors() {
    static Colors s = Colors::dark(kDefaultAccent);
    return s;
}

inline const Colors& Colors::current() {
    return activeColors();
}

inline void Colors::activate(bool aDark, AccentColor aAccent) {
    activeColors() = aDark ? dark(aAccent) : light(aAccent);
}

} // namespace theme

#endif // GUI_THEME_COLORS_H
