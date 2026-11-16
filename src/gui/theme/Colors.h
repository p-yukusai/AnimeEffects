#ifndef GUI_THEME_COLORS_H
#define GUI_THEME_COLORS_H

#include <QColor>
#include <QString>

#include "core/TimeKeyType.h"
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

// Semantic rows. Warnings and errors must read as themselves whatever the
// accent is (the user can pick green, and a warning in green is not a warning),
// so they are the two hue rows the semantic roles read instead of the neutral or
// accent row: Tailwind red for danger, amber for warning.
inline constexpr int kDangerRow = 0;  // red
inline constexpr int kWarningRow = 2; // amber

// Token roles; the column maps are indexed by these.
enum TokenRole {
    kBase, kRecessed, kRaised, kRaisedHover, kActive, kActiveHover,
    kHairline, kHairlineHover, kOutline, kHandleBorder, kFocus, kHover,
    kText, kTextMuted, kTextMutedDim, kTextDisabled, kIcon,
    kSelection, kSelectionText, kTextSelection, kAccent, kAccentSwatch, kAccentBright, kAccentHover, kAccentText,
    kFloaterBody, kFloaterEdge,
    kRecessedGuideLine, // dashed interactive guides on the recessed canvas
                       // (spline editor): 2 lightness steps from the recessed
                       // column in both themes
    kRecessedHairline, // 1px informational lines on the recessed canvas:
                       // 1 step from the recessed column, quieter than the
                       // guides
    kWindowBand, // temporary playback window: translucent band across the
                 // ruler. Neutral (structural family), never accent — the
                 // window is a workspace setting, not interactive state, and
                 // accent is reserved for the playhead/selection/marquee.
    kWindowEdge, // window bracket stems and label pills: a solid neutral
                 // step above the band
    kWindowText, // window label pill text
    kAccentBrightText, // text on accentBright fills (the playhead pill):
                       // max-contrast against the saturated accentBright
    kDanger,  // semantic error ink. red-600 on the pale theme, red-400 on the
              // deep one - both clear their background, and neither follows the
              // accent (an error must read as one whatever hue the user picked)
    kWarning, // semantic warning ink, same rule with amber: amber-700 on the
              // pale theme (amber-600 is too pale for text on white) and
              // amber-400 on the deep one
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
    /* kTextMutedDim      */ 6, // neutral-600 (#525252): one step dimmer than
                                // light textMuted (col 5 = #737373), so muted
                                // picker labels/glyphs read dimmer than the
                                // standard muted text. (In the dark theme this
                                // token reads neutral-500 #737373 — see the
                                // dark table — which is where the mock's
                                // --text-muted lives; the picker is only
                                // compared in dark.)
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
    /* kRecessedGuideLine */ 3, // 2 steps from the recessed column (light
                                 // col 1) — same distance as dark
    /* kRecessedHairline  */ 2, // 1 step from the recessed column (light
                                 // col 1)
    /* kWindowBand        */ 3, // neutral-300: between hairline (2) and the
                                 // muted text (4) — a visible gray wash
    /* kWindowEdge        */ 5, // neutral-500: solid, readable on both themes
    /* kWindowText        */ 0, // neutral-50
    /* kAccentBrightText  */ 0, // neutral-50: lightest text on the saturated
                                 // accentBright
    /* kDanger            */ 6, // red-600 (#dc2626). These two index the
                                 // red/amber rows (kDangerRow/kWarningRow), not
                                 // the neutral row the surface tokens read
    /* kWarning           */ 7, // amber-700 (#b45309), the step the QML facts
                                 // rows already used: amber-600 is too pale for
                                 // text on white
};
static_assert(sizeof(kTailwindLight) / sizeof(kTailwindLight[0]) == kTokenCount,
              "kTailwindLight must have one column per TokenRole");
static constexpr int kTailwindDark[] = {
    // The dark elevation model was one step too bright: base read as the same
    // neutral-800 card surface the mockup used for RAISED controls. Shift the
    // surfaces down one column so base/recessed are the floor, `raised` is the
    // old base, and content keeps its existing contrast. Light is unchanged.
    /* kBase              */ 9,
    /* kRecessed          */ 10,
    /* kRaised            */ 8,
    /* kRaisedHover       */ 7,
    /* kActive            */ 7, // one step above `raised`; not the same fill
    /* kActiveHover       */ 6,
    // Borders follow the surfaces down too. Keeping the old absolutes would
    // turn every separator into a 2-step jump against the darker base; these
    // stay one step above the surface they sit on.
    /* kHairline          */ 8, // one step above base (neutral-800)
    /* kHairlineHover     */ 7, // one step above hairline
    /* kOutline           */ 7, // one step above raised
    /* kHandleBorder      */ 6,
    /* kFocus             */ 4,
    /* kHover             */ 6, // one step above outline, not text strength
    /* kText              */ 1,
    /* kTextMuted         */ 4,
    /* kTextMutedDim      */ 5, // neutral-500 (#737373): a step dimmer than
                                // textMuted (dark col 4 = neutral-400). The
                                // mock's muted text is #737373, so muted
                                // picker elements read this dim token.
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
    /* kFloaterBody       */ 8,
    /* kFloaterEdge       */ 7, // normal edge; the body is one step above base
    /* kRecessedGuideLine */ 8, // 2 steps from the recessed column (dark
                                 // col 10) — same distance as light
    /* kRecessedHairline  */ 9, // 1 step from the recessed column (dark
                                 // col 10)
    /* kWindowBand        */ 5, // neutral-500: mid gray, visible on the dark
                                 // ruler at its fixed alpha
    /* kWindowEdge        */ 4, // neutral-400 (same step as textMuted: a
                                 // bright neutral for the stems and pills)
    /* kWindowText        */ 9, // neutral-900
    /* kAccentBrightText  */ 9, // neutral-900: darkest text on the pale
                                 // accentBright
    /* kDanger            */ 4, // red-400 (#f87171) on the deep theme
    /* kWarning           */ 4, // amber-400 (#fbbf24) on the deep theme
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
                     kKeyOpacity, kKeyBone, kKeyPose, kKeyMesh, kKeyFFD,
                     kKeyImage, kKeyHSV, kKeyBlur, kKeyWave, kKeyMask, kKeyCount };
static constexpr int kKeyColorRow[] = {
    kTailwindNeutralRow, // baseKey  (neutral)
    11,                  // moveKey  (indigo)
    13,                  // rotateKey (purple)
    14,                  // scaleKey (fuchsia)
    16,                  // depthKey (rose)
    2,                   // opacityKey   (amber)
    4,                   // boneKey  (lime)
    5,                   // poseKey  (green)
    6,                   // meshKey  (emerald)
    8,                   // FFDKey   (cyan)
    9,                   // imageKey (sky)
    11,                  // HSVKey   (indigo)
    14,                  // blurKey  (fuchsia)
    7,                   // waveKey  (teal)
    12,                  // maskKey  (violet)
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
// THE CHANNEL HUE TABLE (design section 7, "One colour per channel").
//
// A graph channel is ONE COMPONENT of a key type (ctrl/graph/graph_Channel.h:
// "Move X", "Move Y", "Scale X", ...). This is the one colour assignment both
// views read - the timeline (ctrl/time/time_Renderer.cpp) and the graph panel
// (theme::Graph / gui::buildGraphPanelModel) - so a channel's hue can never
// disagree between the lane it is edited on and the curve it is drawn as.
//
// THE RULE: axis where there is an axis, type otherwise.
//   * A component that lies on a layout axis takes that axis's hue: X red,
//     Y green, Z blue. These are the palette's rose, green and sky rows at the
//     key column (kKeyColorColumnLight/Dark) - the design's own cells: dark
//     #fb7185 (rose-400), #4ade80 (green-400), #38bdf8 (sky-400), and their
//     one-step-deeper light counterparts rose-600/green-600/sky-600.
//   * Every other channel keeps its key type's hue (the kKeyColorRow table
//     above), so opacity stays amber, an angle stays its type's hue, etc.
//   * A type's FIRST component is what the timeline draws its key and its lane
//     label in: the X component (or Blur's first radius) is X, so Move, Scale,
//     Image and Blur read red on the timeline, and their second component reads
//     green in the panel. The type's own name on the lane/chip/group is what says
//     Move vs Scale.
//
// WHICH COMPONENTS LIE ON AN AXIS. The first and second component of every
// type whose values are an X/Y pair: Move, Scale and Image (named "X"/"Y") and
// Blur (its two radii, "Radius X"/"Radius Y"). Blur's third component is an
// angle. Rotate's angle, Depth, Opacity and HSV's three values have no axis. No
// graphable channel is a Z today: Depth is a value the design leaves on its
// type's hue, and the two angles are not axes - the Z cell is stated for
// completeness of the convention.
// ---------------------------------------------------------------------------
enum ChannelAxis { kChannelAxisX, kChannelAxisY, kChannelAxisZ, kChannelAxisCount };
static constexpr int kChannelAxisRow[] = {
    16, // X: rose  (#fb7185 dark / #e11d48 light)
    5,  // Y: green (#4ade80 dark / #16a34a light)
    9,  // Z: sky   (#38bdf8 dark / #0284c7 light)
};
static_assert(sizeof(kChannelAxisRow) / sizeof(kChannelAxisRow[0]) == kChannelAxisCount,
              "kChannelAxisRow must have one row per ChannelAxis");

// The layout axis of one channel, or -1 for a channel with no axis.
inline int channelAxis(core::TimeKeyType aType, int aComponent) {
    switch (aType) {
    case core::TimeKeyType_Move:
    case core::TimeKeyType_Scale:
    case core::TimeKeyType_Image:
    case core::TimeKeyType_Blur:
        if (aComponent == 0) return kChannelAxisX;
        if (aComponent == 1) return kChannelAxisY;
        return -1;
    default:
        return -1;
    }
}

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
    QColor textMutedDim;  // one step dimmer than textMuted: muted picker
                          // labels/glyphs. The mock's --text-muted is
                          // #737373, but the dark theme's textMuted reads
                          // cleaner (neutral-400), so muted elements that
                          // must match the mock use this dim token.
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
    QColor recessedGuideLine; // dashed interactive guides on the recessed
                             // canvas (spline editor): equidistant from the
                             // recessed column in both themes (2 steps), so
                             // the guides read at the same strength on the
                             // sunken surface of either theme
    QColor recessedHairline; // 1px informational lines on the recessed
                             // canvas (spline axis, preview track): 1 step
                             // from the recessed column, quieter than the
                             // guides
    QColor windowBand;   // temporary playback window: translucent band fill
    QColor windowEdge;   // window bracket stems and label pills
    QColor windowText;   // window label pill text
    QColor accentBrightText; // text on accentBright fills (playhead pill)
    // Per-key colors for easier identification: one Tailwind row per key
    // type (kKeyColorRow) at a shared per-theme column, so all keys read at
    // the same lightness; baseKey is the neutral key color.
    QColor baseKey; // Equal to TimeKeyType_TERM
    QColor moveKey;
    QColor rotateKey;
    QColor scaleKey; // Scale, rotate & transform
    QColor depthKey;
    QColor opacityKey;
    QColor boneKey;
    QColor poseKey;
    QColor meshKey;
    QColor FFDKey;
    QColor imageKey;
    QColor HSVKey;
    QColor blurKey;
    QColor waveKey;
    // The MATTE's own hue. A mask is no longer the neutral fallback: an uncoloured key
    // dot reads as "no state" beside every other channel's hue, which is what a mask
    // looked like in the card before this token existed.
    QColor maskKey;
    // The channel hue table's axis cells (see THE CHANNEL HUE TABLE): X red,
    // Y green, Z blue. Both views read these through channelColor() below;
    // no widget derives an axis hue of its own.
    QColor axisX;
    QColor axisY;
    QColor axisZ;

    // Semantic inks, the two roles the token table has no cell for: an error and
    // a warning have to read as such whatever the accent is, so they are the red
    // and amber rows at their own theme column instead of a cell of the neutral
    // (achromatic) or accent row. They live here rather than as literals in the
    // QML bridge because the command log's warning badge and the QML facts rows
    // must be the same ink, and a literal in a second file is how the two drift.
    QColor danger;  // red-600 / red-400
    QColor warning; // amber-700 / amber-400

    // THE ALPHA CHECKER behind a layer thumbnail (and anything else that shows a matte).
    // These are the values the ORIGINAL translucent pair composited to over the tree's #262626
    // row (#363636 on #202020), written out as exact channels instead of reached through
    // alpha: a translucent pair landed one level off on screen (measured 31 for the dark tile
    // where the specification says 32), and an opaque pair cannot drift.
    QColor checkerLight;
    QColor checkerDark;

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
            paletteColor(n[kTailwindLight[kTextMutedDim]]), // textMutedDim
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
            paletteColor(n[kTailwindLight[kRecessedGuideLine]]),   // recessedGuideLine
            paletteColor(n[kTailwindLight[kRecessedHairline]]), // recessedHairline
            paletteColor(n[kTailwindLight[kWindowBand]], 0.45), // windowBand
            paletteColor(n[kTailwindLight[kWindowEdge]]),       // windowEdge
            paletteColor(n[kTailwindLight[kWindowText]]),       // windowText
            paletteColor(n[kTailwindLight[kAccentBrightText]]), // accentBrightText
            // Per-key colors: kKeyColorRow at one shared column per theme
            // (kKeyColorColumnLight), so all keys read at the same lightness.
            paletteColor(kTailwindRows[kKeyColorRow[kKeyBase]][kKeyColorColumnLight]), // baseKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyMove]][kKeyColorColumnLight]), // moveKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyRotate]][kKeyColorColumnLight]), // rotateKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyScale]][kKeyColorColumnLight]), // scaleKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyDepth]][kKeyColorColumnLight]), // depthKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyOpacity]][kKeyColorColumnLight]), // opacityKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyBone]][kKeyColorColumnLight]), // boneKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyPose]][kKeyColorColumnLight]), // poseKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyMesh]][kKeyColorColumnLight]), // meshKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyFFD]][kKeyColorColumnLight]), // FFDKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyImage]][kKeyColorColumnLight]), // imageKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyHSV]][kKeyColorColumnLight]), // HSVKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyBlur]][kKeyColorColumnLight]), // blurKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyWave]][kKeyColorColumnLight]), // waveKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyMask]][kKeyColorColumnLight]), // maskKey
            // The channel hue table's axis cells, at the same key column as
            // the type hues so an axis channel and a no-axis channel read at
            // one brightness (THE CHANNEL HUE TABLE).
            paletteColor(kTailwindRows[kChannelAxisRow[kChannelAxisX]][kKeyColorColumnLight]), // axisX
            paletteColor(kTailwindRows[kChannelAxisRow[kChannelAxisY]][kKeyColorColumnLight]), // axisY
            paletteColor(kTailwindRows[kChannelAxisRow[kChannelAxisZ]][kKeyColorColumnLight]), // axisZ
            // The semantic roles read their own hue rows (never the neutral or
            // accent row): red and amber at the pale-theme columns above.
            paletteColor(kTailwindRows[kDangerRow][kTailwindLight[kDanger]]),   // danger
            paletteColor(kTailwindRows[kWarningRow][kTailwindLight[kWarning]]), // warning
            // the SAME two steps round a pale row (`base` = #ffffff in the light theme)
            QColor(0xf4, 0xf4, 0xf4), // checkerLight  (#f4f4f4)
            QColor(0xdc, 0xdc, 0xdc), // checkerDark   (#dcdcdc)
            false, // isDark
        };
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
            paletteColor(n[kTailwindDark[kTextMutedDim]]), // textMutedDim
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
            paletteColor(n[kTailwindDark[kRecessedGuideLine]]),   // recessedGuideLine
            paletteColor(n[kTailwindDark[kRecessedHairline]]), // recessedHairline
            paletteColor(n[kTailwindDark[kWindowBand]], 0.30), // windowBand
            paletteColor(n[kTailwindDark[kWindowEdge]]),       // windowEdge
            paletteColor(n[kTailwindDark[kWindowText]]),       // windowText
            paletteColor(n[kTailwindDark[kAccentBrightText]]), // accentBrightText
            // Per-key colors: kKeyColorRow at one shared column per theme
            // (kKeyColorColumnDark), so all keys read at the same lightness.
            paletteColor(kTailwindRows[kKeyColorRow[kKeyBase]][kKeyColorColumnDark]), // baseKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyMove]][kKeyColorColumnDark]), // moveKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyRotate]][kKeyColorColumnDark]), // rotateKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyScale]][kKeyColorColumnDark]), // scaleKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyDepth]][kKeyColorColumnDark]), // depthKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyOpacity]][kKeyColorColumnDark]), // opacityKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyBone]][kKeyColorColumnDark]), // boneKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyPose]][kKeyColorColumnDark]), // poseKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyMesh]][kKeyColorColumnDark]), // meshKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyFFD]][kKeyColorColumnDark]), // FFDKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyImage]][kKeyColorColumnDark]), // imageKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyHSV]][kKeyColorColumnDark]), // HSVKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyBlur]][kKeyColorColumnDark]), // blurKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyWave]][kKeyColorColumnDark]), // waveKey
            paletteColor(kTailwindRows[kKeyColorRow[kKeyMask]][kKeyColorColumnDark]), // maskKey
            // The channel hue table's axis cells (THE CHANNEL HUE TABLE): rose,
            // green and sky at the dark key column = #fb7185, #4ade80, #38bdf8,
            // the three cells design section 7 names.
            paletteColor(kTailwindRows[kChannelAxisRow[kChannelAxisX]][kKeyColorColumnDark]), // axisX
            paletteColor(kTailwindRows[kChannelAxisRow[kChannelAxisY]][kKeyColorColumnDark]), // axisY
            paletteColor(kTailwindRows[kChannelAxisRow[kChannelAxisZ]][kKeyColorColumnDark]), // axisZ
            // red-400 / amber-400: the deep theme's semantic steps (see
            // kDangerRow/kWarningRow for why they do not follow the accent).
            paletteColor(kTailwindRows[kDangerRow][kTailwindDark[kDanger]]),   // danger
            paletteColor(kTailwindRows[kWarningRow][kTailwindDark[kWarning]]), // warning
            // the checker's ON-SCREEN values, measured off the original's own screenshot
            // (c0b1eea4): #242424 on #141414. The tree's row is `base` #171717 - not `raised`
            // #262626, which is what I had been calibrating against, one surface too high.
            QColor(0x24, 0x24, 0x24), // checkerLight  (#242424 = 36)
            QColor(0x14, 0x14, 0x14), // checkerDark   (#141414 = 20)
            true, // isDark
        };
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

// A key type's own hue, i.e. the channel hue of a type with no axis. This is
// the kKeyColorRow row for every type (including bone/pose/mesh/FFD, which the
// TIMELINE draws; the graph cannot reach them - componentCount() is 0 - so the
// panel's table does not list them). baseKey is the neutral fallback.
inline QColor keyTypeColor(const Colors& aColors, core::TimeKeyType aType) {
    switch (aType) {
    case core::TimeKeyType_Move: return aColors.moveKey;
    case core::TimeKeyType_Rotate: return aColors.rotateKey;
    case core::TimeKeyType_Scale: return aColors.scaleKey;
    case core::TimeKeyType_Depth: return aColors.depthKey;
    case core::TimeKeyType_Opacity: return aColors.opacityKey;
    case core::TimeKeyType_Bone: return aColors.boneKey;
    case core::TimeKeyType_Pose: return aColors.poseKey;
    case core::TimeKeyType_Mesh: return aColors.meshKey;
    case core::TimeKeyType_FFD: return aColors.FFDKey;
    case core::TimeKeyType_Image: return aColors.imageKey;
    case core::TimeKeyType_HSV: return aColors.HSVKey;
    case core::TimeKeyType_Blur: return aColors.blurKey;
    case core::TimeKeyType_Wave: return aColors.waveKey;
    case core::TimeKeyType_Mask: return aColors.maskKey;
    case core::TimeKeyType_TERM: break;
    }
    return aColors.baseKey; // the neutral key
}

// THE TABLE. One channel of one key type -> its hue: the axis hue where the
// component lies on an axis, the type's own hue otherwise (THE CHANNEL HUE
// TABLE above states the rule and the cells). Both views call this and nothing
// else:
//   * ctrl/time/time_Renderer.cpp draws a timeline key - which belongs to the
//     TYPE, not to a component - in channelColor(colors, type, 0), and its lane
//     label in the same hue at 0.42;
//   * gui::buildGraphPanelModel fills PanelChannelRow::color with
//     channelColor(colors, channel.type(), channel.component()), and the
//     drawing layer inks that row's curve, markers and chip with it.
inline QColor channelColor(const Colors& aColors, core::TimeKeyType aType, int aComponent) {
    switch (channelAxis(aType, aComponent)) {
    case kChannelAxisX: return aColors.axisX;
    case kChannelAxisY: return aColors.axisY;
    case kChannelAxisZ: return aColors.axisZ;
    default: return keyTypeColor(aColors, aType);
    }
}

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
