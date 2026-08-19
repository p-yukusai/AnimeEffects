#ifndef GUI_THEME_COLORS_H
#define GUI_THEME_COLORS_H

#include <QColor>
#include <QString>

#include <algorithm>
#include <cmath>

namespace theme {

// ---------------------------------------------------------------------------
// Oklch -> sRGB. Every token below is authored in Oklch (L = perceptual
// lightness, C = chroma, H = hue in degrees) because it is far easier to
// reason about distance and elevation in L than in hex/RGB.
// ---------------------------------------------------------------------------
inline QColor oklch(double aL, double aC, double aH, double aAlpha = 1.0) {
    const double h = aH * 3.14159265358979323846 / 180.0;
    const double a = aC * std::cos(h);
    const double b = aC * std::sin(h);
    const double l_ = aL + 0.3963377774 * a + 0.2158037573 * b;
    const double m_ = aL - 0.1055613458 * a - 0.0638541728 * b;
    const double s_ = aL - 0.0894841775 * a - 1.2914855480 * b;
    const double l = l_ * l_ * l_;
    const double m = m_ * m_ * m_;
    const double s = s_ * s_ * s_;
    const double r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    const double g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    const double bb = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
    auto toSrgb = [](double x) {
        x = std::clamp(x, 0.0, 1.0);
        return (float)(x <= 0.0031308 ? 12.92 * x : 1.055 * std::pow(x, 1.0 / 2.4) - 0.055);
    };
    return QColor::fromRgbF(toSrgb(r), toSrgb(g), toSrgb(bb), (float)aAlpha);
}

// Selectable accent hues (Oklch H, degrees). The user picks one of these.
inline constexpr double kDefaultHue = 279.0;
inline const QVector<double>& accentHues() {
    static const QVector<double> kHues{26.0, 56.0, 90.0, 147.0, 198.0, 220.0, 279.0, 323.0};
    return kHues;
}

// Per-hue tuning for the accent-family brand tokens (accent, focus/
// accentBright): a lightness offset and a chroma factor per hue. Unified
// lightness+chroma makes the warm hues (orange, yellow) render muddy and the
// cool primaries muted in dark; the tuning lifts them so they read vivid.
// The selection token intentionally does NOT follow this: it is the subtle
// highlight layer (layer selection, hover pills) and must stay quiet.
inline void hueAccentTuning(double aHue, bool aDark, double& aLightnessOffset, double& aChromaFactor) {
    struct Entry { double hue; double darkL, lightL, darkC, lightC; };
    static const Entry kTuning[] = {
        { 26.0,  0.09, 0.00, 1.27, 1.15 }, // red
        { 56.0,  0.19, 0.02, 1.33, 1.15 }, // orange
        { 90.0,  0.27, 0.05, 1.07, 1.10 }, // yellow (high L, modest C)
        { 147.0, 0.06, 0.00, 1.07, 1.05 }, // green
        { 198.0, 0.11, 0.00, 1.07, 1.05 }, // teal
        { 220.0, 0.07, 0.00, 1.20, 1.10 }, // blue (deeper so it reads blue, not cyan next to the teal)
        { 279.0, 0.07, 0.00, 1.20, 1.10 }, // indigo
        { 323.0, 0.09, 0.00, 1.13, 1.05 }, // magenta
    };
    for (const auto& e : kTuning) {
        if (std::abs(e.hue - aHue) < 0.5) {
            if (aDark) { aLightnessOffset = e.darkL; aChromaFactor = e.darkC; }
            else { aLightnessOffset = e.lightL; aChromaFactor = e.lightC; }
            return;
        }
    }
    aLightnessOffset = 0.0;
    aChromaFactor = 1.0;
}


// ---------------------------------------------------------------------------
// Design tokens for an appearance (light/dark x accent hue).
//
// This is the single source of truth for every surface the custom-painted
// widgets draw, for the QPalette, for the stylesheet templates, and for the
// runtime-tinted icons. A color is a pure function of (dark, hue); it is
// computed once per appearance change and cached (see activate/current).
//
// Every non-text token shares the selected hue; only chroma (and lightness)
// varies across tokens, and lightness flips between the light and dark sets.
// At C=0 the hue is inert, so the neutrals still render grayscale. Text stays
// neutral (H=0) for legibility.
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
    QColor focus;         // focus ring / active border (accent hue)
    QColor hover;         // hover border, splitter drag, scrollbar pill active (neutral)
    QColor text;          // primary text
    QColor textMuted;     // secondary text
    QColor textDisabled;  // disabled text
    QColor icon;          // resting content on buttons (icons, pill text):
                          // dimmer than `text` so the active state reads
    QColor selection;     // brand selection / highlight fill
    QColor selectionText; // text on selection
    QColor accent;        // brand checked/active fill (active-mode, link)
    QColor accentBright;  // brand bright edge (playhead, marquee, focus ring)
    QColor accentHover;   // menu/combo popup hover fill: a stronger accent
                          // tint than `selection` (which stays quiet)
    QColor accentText;    // content on accent fills (checked icons/pill text):
                          // max-contrast (white in dark, black in light)
    QColor floaterBody;   // menu/combo popup panel: dark stays sunken, light
                          // flips to white (the proxy draws a shadow there)
    QColor floaterEdge;   // popup panel edge
    // Per-key colors for easier identification.
   // https://supercolorpalette.com/?scp=eJxl0sluwyAQBuB34fxHYjMMvnnjJaIc3EVJpbaRmuUS5d07BIwVRb7w4_lY7LmJd9Fub-JHtOJwugqIA4_m3_n7uD9eTpxP_N44bDSUgtmhgFSmo-4ayzVzDZPj-Cba89_lE-LKpdp6NBLGIMjdHVk6ZTovi8xhGkUBZGAJ1iKYCmiwg6cCcohNAUYFWIemQXAVdNRMbjlbDnFYAJfy8s4hhAoGG3qvC8ghxgVIWA3voeR6h9FPknwBOVTAS1sJoifQhziEsYAcKlBSwxCf50k4EyMtt85hFYZvoFL5E-m4Irp67xRW4i1YvZBxzDVzDZVoxb9Ov5C-600lOayE_4WRL2Tqeqokh5WQhqaV8AOx50a7pV56-C2fw8ld7sf0bdP4-zGv0pBX3YZlx1SU1SZ1EhW2IVBRWkLbwh7Td1Z70Urub97XQ5zTMT--zuL-D5svvOQ
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
    double hue; // the accent hue these tokens were computed with

    static Colors light(double aHue = kDefaultHue) {
        double d = 0.0, cF = 1.0;
        hueAccentTuning(aHue, false, d, cF);
        Colors c{
            oklch(1.00, 0, aHue),      // base
            oklch(0.96, 0, aHue),      // recessed (same as raised)
            oklch(0.96, 0, aHue),      // raised
            oklch(0.94, 0, aHue),      // raisedHover
            oklch(0.90, 0, aHue),      // active
            oklch(0.86, 0, aHue),      // activeHover
            oklch(0.96, 0, aHue),      // hairline
            oklch(0.92, 0, aHue),      // hairlineHover
            oklch(0.85, 0, aHue),      // outline
            oklch(0.52 + d, 0.23 * cF, aHue), // focus
            oklch(0.78, 0, aHue),      // hover
            oklch(0.22, 0, 0),         // text (neutral)
            oklch(0.53, 0, 0),         // textMuted (neutral)
            oklch(0.69, 0, 0),         // textDisabled (neutral)
            oklch(0.22, 0, 0),         // icon (resting content == text)
            // selection: the accent itself at low opacity (40%) — a
            // translucent overlay, not a darker color. A darker color at the
            // same hue falls out of gamut and renders redder than the theme
            // (the old fixed-L selection was #832100 at hue 36 for orange);
            // the translucent accent keeps the hue and only lowers the
            // effective lightness. The timeline already uses this pattern
            // (accent-hue track selection at 40% alpha in dark).
            oklch(0.90 + d, 0.15 * cF, aHue, 0.40),
            oklch(0.22, 0, 0),         // selectionText (neutral)
            oklch(0.90 + d, 0.15 * cF, aHue), // accent
            oklch(0.52 + d, 0.23 * cF, aHue), // accentBright
            oklch(0.88, 0.15 * cF * 0.9, aHue), // accentHover (popup hover:
                                       // stronger tint, 0.02 below the
                                       // previous L, chroma scaled per hue)
            oklch(0.00, 0, 0),         // accentText (max-contrast content)
            oklch(1.00, 0, aHue),      // floaterBody (white; shadow separates)
            oklch(0.96, 0, aHue),      // floaterEdge
            // In RGB because the oklch formula doesn't play nice with my values
            {50, 50, 50}, // key base
            {47, 42, 84}, // move
            {97, 58, 112}, // rotate
            {140, 76, 120}, // scale
            {168, 94, 100}, // depth
            {196, 155, 114}, // opa
            {215, 224, 135}, // bone
            {185, 252, 157}, // pose
            {99, 255, 136}, // mesh
            {168, 255, 246}, // ffd
            {173, 214, 255}, // image
            {186, 179, 255}, // hsv
            {234, 184, 255} // blur
        };
        c.isDark = false;
        c.hue = aHue;
        return c;
    }

    static Colors dark(double aHue = kDefaultHue) {
        double d = 0.0, cF = 1.0;
        hueAccentTuning(aHue, true, d, cF);
        Colors c{
            oklch(0.27, 0, aHue),      // base
            oklch(0.24, 0, aHue),      // recessed
            oklch(0.35, 0, aHue),      // raised
            oklch(0.37, 0, aHue),      // raisedHover
            oklch(0.50, 0, aHue),      // active
            oklch(0.56, 0, aHue),      // activeHover
            oklch(0.33, 0, aHue),      // hairline
            oklch(0.45, 0, aHue),      // hairlineHover
            oklch(0.45, 0, aHue),      // outline
            oklch(0.56 + d, 0.23 * cF, aHue), // focus
            oklch(0.59, 0, aHue),      // hover
            oklch(0.96, 0, 0),         // text (neutral)
            oklch(0.76, 0, 0),         // textMuted (neutral)
            oklch(0.57, 0, 0),         // textDisabled (neutral)
            oklch(0.93, 0, 0),         // icon (resting content: dimmer than text)
            // selection: the accent at 40% opacity — a translucent overlay,
            // not a darker color (see the light() comment; the timeline
            // track selection uses the same 40% alpha in dark).
            oklch(0.46 + d, 0.15 * cF, aHue, 0.40),
            oklch(0.96, 0, 0),         // selectionText (neutral)
            oklch(0.46 + d, 0.15 * cF, aHue), // accent
            oklch(0.56 + d, 0.23 * cF, aHue), // accentBright
            // popup hover: a stronger accent (L+0.03, C*1.1 relative to the
            // accent above; 0.02 below the previous hover L). Derived from
            // the tuned accent instead of a fixed lightness: at a fixed L
            // the warm hues fall outside their gamut and clamp to brown/red
            // (yellow read as #8f5600 orange, orange as red).
            oklch(0.49 + d, 0.165 * cF, aHue),
            oklch(1.00, 0, 0),         // accentText (max-contrast content)
            oklch(0.24, 0, aHue),      // floaterBody (sunken)
            oklch(0.27, 0, aHue),      // floaterEdge
            // In RGB because the oklch formula doesn't play nice with my values
            {232, 232, 232}, // key base
            {47, 42, 84}, // move
            {97, 58, 112}, // rotate
            {140, 76, 120}, // scale
            {168, 94, 100}, // depth
            {196, 155, 114}, // opa
            {215, 224, 135}, // bone
            {185, 252, 157}, // pose
            {99, 255, 136}, // mesh
            {168, 255, 246}, // ffd
            {173, 214, 255}, // image
            {186, 179, 255}, // hsv
            {234, 184, 255} // blur
        };
        c.isDark = true;
        c.hue = aHue;
        return c;
    }

    // The active palette. Computed once per appearance change by activate();
    // callers (custom-paint widgets, QSS substitution) read this instead of
    // re-deriving colors per paint event.
    static const Colors& current();

    // Recompute and cache the active palette for a (dark, hue) appearance.
    static void activate(bool aDark, double aHue);

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
    static Colors s = Colors::dark(kDefaultHue);
    return s;
}

inline const Colors& Colors::current() {
    return activeColors();
}

inline void Colors::activate(bool aDark, double aHue) {
    activeColors() = aDark ? dark(aHue) : light(aHue);
}

} // namespace theme

#endif // GUI_THEME_COLORS_H
